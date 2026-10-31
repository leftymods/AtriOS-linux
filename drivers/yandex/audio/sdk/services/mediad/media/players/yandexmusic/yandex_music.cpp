#include "yandex_music.h"

#include "authorization_failed_exception.h"
#include "request_timeout_exception.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/telemetry/telemetry.h>

#include <util/generic/scope.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

YIO_DEFINE_LOG_MODULE("media");

using namespace quasar;

namespace {
    const char* FIELD_API_URL = "apiUrl";

    const int DEFAULT_CONNECTION_FIRST_DELAY_MS = 1000;
    const int DEFAULT_CONNECTION_MAX_DELAY_MS = 30 * DEFAULT_CONNECTION_FIRST_DELAY_MS;
    const int DEFAULT_CONNECTION_RETRY_DELAY_FACTOR = 2;

    std::chrono::milliseconds getMillisSince(const std::chrono::steady_clock::time_point& timePoint) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - timePoint);
    }

    /**
     * Сериализует упрощенную модель track в json для отправки на сервер в составе текущей очереди.
     * @return json упрощенной модели трека.
     */
    Json::Value trackToJson(const YandexMusic::Track& track) {
        Json::Value json;
        json["id"] = track.id;
        json["batchId"] = track.batchId;
        json["batchInfo"] = track.batchInfo;

        return json;
    }

    Json::Value trackToJson(const std::shared_ptr<const YandexMusic::Track>& track) {
        if (!track) {
            return Json::objectValue;
        }
        return trackToJson(*track);
    }

    std::optional<YandexMusic::Track::Normalization> extractNormalization(const Json::Value& responseData, double targetLufs) {
        if (responseData.isMember("r128")) {
            const auto& r128 = responseData["r128"];
            if (r128["i"].isDouble() && r128["tp"].isDouble()) {
                YandexMusic::Track::Normalization normalization;
                normalization.integratedLoudness = r128["i"].asDouble();
                normalization.truePeak = r128["tp"].asDouble();
                // apply target lufs from quasmodrom
                normalization.targetLufs = targetLufs;
                return normalization;
            }
        }
        return std::nullopt;
    }

    std::string trackStatusToString(const YandexMusic::STATUS& status) {
        switch (status)
        {
            case YandexMusic::STATUS::LIKED:
                return "liked";
            case YandexMusic::STATUS::DISLIKED:
                return "disliked";
            case YandexMusic::STATUS::UNKNOWN:
                return "unknown";
            default:
                throw std::runtime_error("unknown track type");
        }
    }

} // namespace

const int YandexMusic::RETRIES_COUNT_BEFORE_RECONNECT = 3;
const int YandexMusic::RETRIES_COUNT = 5;
const int YandexMusic::WS_REQUEST_TIMEOUT_MS = 3000;
const int YandexMusic::TOTAL_REQUEST_TIMEOUT_MS = 10000;
const int YandexMusic::PING_INTERVAL_SEC = 30;
const bool YandexMusic::SHOULD_RETRY_PING = true;
const bool YandexMusic::GENERATE_RETRY_ID = true;
const int YandexMusic::CONNECT_TIMEOUT_MS = 5000;
const int YandexMusic::PROMISES_LEAK_LIMIT = 5;

static std::shared_ptr<YandexMusic::Track> shotify(const std::shared_ptr<YandexMusic::Track>& track) {
    if (!track) {
        YIO_LOG_ERROR_EVENT("YandexMusic.FailedShotify.NullTrack", "YandexMusic cannot shotify null track");
        return nullptr;
    }

    auto result = std::make_shared<YandexMusic::Track>();
    result->id = track->shotId; // replace track->id;
    result->batchId = track->batchId;
    result->batchInfo = track->batchInfo;
    result->url = track->shotUrl; // replace track->url
    result->status.store(track->status.load());
    result->fullJsonInfo = track->fullJsonInfo;
    result->artists = track->artists;
    result->title = track->title;
    result->coverUri = track->coverUri;
    result->type = track->type;
    result->albumGenre = track->albumGenre;
    result->durationMs = track->shotDurationMs; // replace track->durationMs;
    result->shotUrl = track->shotUrl;
    result->isShot = true; // replace
    return result;
}

YandexMusic::YandexMusic(std::shared_ptr<YandexIO::IDevice> device, const YandexMusic::Params& params,
                         const Json::Value& customYandexMusicConfig, OnAuthFailedHandler onAuthFailedHandler)
    : device_(std::move(device))
    , websocketClient_(device_->telemetry())
    , stopped_(false)
    , isConnected_(false)
    , isAuthorized_(false)
    , needSync_(true)
    , onAuthFailedHandler_(std::move(onAuthFailedHandler))
    , normalizationTargetLufs_(tryGetDouble(customYandexMusicConfig, "normalizationTargetLufs", Track::Normalization::DEFAULT_TARGET_LUFS))
    , shot_(nullptr)
{
    // turn off internal reconnect, as this class handles reconnection by itself
    wsSettings_.reconnect.enabled = false;

    if (params.ssl == Params::Ssl::Yes) {
        auto commonConfig = device_->configuration()->getServiceConfig("common");
        wsSettings_.tls.crtFilePath = getString(commonConfig, "caCertsFile");
    } else {
        wsSettings_.tls.disabled = true;
    }
    wsSettings_.url = getString(device_->configuration()->getServiceConfig("mediad"), "apiUrl");

    std::string customApiUrl = tryGetString(customYandexMusicConfig, FIELD_API_URL);
    if (!customApiUrl.empty()) {
        wsSettings_.url = std::move(customApiUrl);
        customApiUrl_ = true;
        YIO_LOG_INFO("Create YandexMusic with custom url: " << wsSettings_.url);
    }

    wsSettings_.connectTimeoutMs = tryGetInt(customYandexMusicConfig, "connectTimeoutMs", CONNECT_TIMEOUT_MS);

    updatePingSettings(customYandexMusicConfig);
    updateConnectionSettings(customYandexMusicConfig);
    updateWebsocketLogChannels(customYandexMusicConfig);
    updateBitRateSettings(customYandexMusicConfig);

    forcedTrackUrl_ = tryGetString(customYandexMusicConfig, "forcedTrackUrl");

    websocketClient_.setOnConnectHandler(std::bind(&YandexMusic::onConnect, this));
    websocketClient_.setOnMessageHandler(std::bind(&YandexMusic::onMessage, this, std::placeholders::_1));
    websocketClient_.setOnDisconnectHandler(std::bind(&YandexMusic::onDisconnect, this, std::placeholders::_1));
    websocketClient_.setOnFailHandler(std::bind(&YandexMusic::onFail, this, std::placeholders::_1));

    connectionThread_ = std::thread(&YandexMusic::connectionLoop, this);
    if (params.autoPing == Params::AutoPing::Yes) {
        pingThread_ = std::thread(&YandexMusic::pingLoop, this);
    }
}

void YandexMusic::setUrlForTrack(const std::shared_ptr<YandexMusic::Track>& track,
                                 const std::chrono::steady_clock::time_point& startRequestTs) {
    if (!track) {
        YIO_LOG_ERROR_EVENT("YandexMusic.FailedSetUrlForTrack.NullTrack", "YandexMusic cannot set url for null track");
        return;
    }

    // NOTE: if track is "shot" (special thing before normal track), we already have the url
    if (track->isShot) {
        YIO_LOG_TRACE("track is shot, we already have the url: " + track->shotUrl);
        return;
    }

    YIO_LOG_TRACE("waiting for trackUrl...");
    requestUrlForTrack(*track, startRequestTs);
    if (track->url.empty()) {
        device_->telemetry()->reportEvent("musicPlayerEmptyUrl");
    }
    YIO_LOG_TRACE("trackUrl: " + track->url);
}

YandexMusic::~YandexMusic() {
    stopped_ = true;
    disconnect();
    pingCondVar_.notify_one();
    connectionCondVar_.notify_one();

    {
        // notify all pending requests
        std::lock_guard<std::mutex> guard(requestMutex_);
        for (auto& requestPromise : requestPromises_) {
            if (requestPromise.second) {
                // set promise with just empty value
                // request will be interrupted because of stopped_ flag
                requestPromise.second->set_value(Json::nullValue);
            }
        }
        requestPromises_.clear();
    }

    if (pingThread_.joinable()) {
        pingThread_.join();
    }
    if (connectionThread_.joinable()) {
        connectionThread_.join();
    }
}

void YandexMusic::ignoreNextEnd() {
    ignoreNextEnd_ = true;
}

void YandexMusic::stop() {
    auto startRequestTs = std::chrono::steady_clock::now();
    auto track = getCurrentTrack();
    if (track && !ignoreNextEnd_.exchange(false)) {
        feedback(track, "end", startRequestTs);
    }
}

std::shared_ptr<YandexMusic::Track> YandexMusic::start() {
    auto startRequestTs = std::chrono::steady_clock::now();

    // initial start, need to get playlist at first
    shot_ = nullptr;
    sync(startRequestTs);
    checkForShot(true);

    auto track = getCurrentTrack();
    if (track) {
        feedback(track, "start", startRequestTs);
        setUrlForTrack(track, startRequestTs);
    } else {
        YIO_LOG_ERROR_EVENT("YandexMusic.FailedStart.NullTrack", "YandexMusic cannot start null track")
    }

    return track;
}

YandexMusic::PlaylistInfo YandexMusic::getCurrentPlaylist() const {
    std::lock_guard<std::mutex> guard(playlistMutex_);
    return playlistInfo_;
}

std::shared_ptr<YandexMusic::Track> YandexMusic::getCurrentTrack() const {
    std::shared_ptr<Track> track;
    {
        std::lock_guard<std::mutex> guard(playlistMutex_);
        if (shot_) {
            return shot_;
        }
        if (playlist_.empty() || index_ < 0 || (unsigned int)index_ >= playlist_.size()) {
            return nullptr;
        }
        track = playlist_[index_];
    }
    return track;
}

std::shared_ptr<YandexMusic::Track> YandexMusic::getNextTrack() const {
    std::lock_guard<std::mutex> guard(playlistMutex_);
    if (index_ < 0 || (unsigned int)index_ + 1 >= playlist_.size()) {
        return nullptr;
    }
    return playlist_[index_ + 1];
}

void YandexMusic::rewindForward(bool skip, const std::chrono::steady_clock::time_point& startRequestTs) {
    auto current = getCurrentTrack();
    if (current) {
        feedback(current, skip ? "skip" : "end", startRequestTs);
    }

    bool shouldSync;
    {
        std::lock_guard<std::mutex> guard(playlistMutex_);
        if (current && !current->isShot) {
            index_++;
            currentPositionSec_ = 0;
        }
        // avoid deadlock in sync
        shouldSync = (unsigned int)index_ >= playlist_.size();
    }

    if (shouldSync) {
        sync(startRequestTs);
    }
}

void YandexMusic::rewindBackward(const std::chrono::steady_clock::time_point& startRequestTs) {
    auto current = getCurrentTrack();
    if (current) {
        feedback(current, "prev", startRequestTs);
    }

    bool shouldSync;
    {
        std::lock_guard<std::mutex> guard(playlistMutex_);
        index_--;
        currentPositionSec_ = 0;
        shot_ = nullptr;
        // avoid deadlock in sync
        shouldSync = index_ <= 0;
    }

    if (shouldSync) {
        sync(startRequestTs);
    }
}

std::shared_ptr<YandexMusic::Track> YandexMusic::startCurrentTrack(
    const std::chrono::steady_clock::time_point& startRequestTs) {
    feedback(getCurrentTrack(), "start", startRequestTs);
    sync(startRequestTs);
    checkForShot();
    auto track = getCurrentTrack();
    if (track) {
        setUrlForTrack(track, startRequestTs);
    }
    return track;
}

std::shared_ptr<YandexMusic::Track> YandexMusic::next(bool skip) {
    auto startRequestTs = std::chrono::steady_clock::now();
    rewindForward(skip, startRequestTs);
    return startCurrentTrack(startRequestTs);
}

std::shared_ptr<YandexMusic::Track> YandexMusic::prev() {
    auto startRequestTs = std::chrono::steady_clock::now();
    rewindBackward(startRequestTs);
    return startCurrentTrack(startRequestTs);
}

std::shared_ptr<YandexMusic::Track> YandexMusic::restart() {
    auto startRequestTs = std::chrono::steady_clock::now();

    shot_ = nullptr;
    sync(startRequestTs);
    rewindForward(true, startRequestTs);
    checkForShot(true);
    auto track = getCurrentTrack();
    feedback(track, "start", startRequestTs);
    setUrlForTrack(track, startRequestTs);
    return track;
}

void YandexMusic::like() {
    auto startRequestTs = std::chrono::steady_clock::now();
    auto track = getCurrentTrack();
    if (!track) {
        YIO_LOG_ERROR_EVENT("YandexMusic.FailedLike.NullTrack", "YandexMusic cannot like null track");
        return;
    }
    auto status = track->status.exchange(STATUS::LIKED);
    YIO_LOG_INFO("Like action. current track status: " << trackStatusToString(status));
    if (status == STATUS::UNKNOWN) {
        feedback(track, "like", startRequestTs);
        sync(startRequestTs);
    } else if (status == STATUS::DISLIKED) {
        feedback(track, "undislike", startRequestTs);
        sync(startRequestTs);
    }
}

void YandexMusic::dislike() {
    auto startRequestTs = std::chrono::steady_clock::now();
    auto track = getCurrentTrack();
    if (!track) {
        YIO_LOG_ERROR_EVENT("YandexMusic.FailedDislike.NullTrack", "YandexMusic cannot dislike null track");
        return;
    }
    auto status = track->status.exchange(STATUS::DISLIKED);
    YIO_LOG_INFO("dislike action. current track status: " << trackStatusToString(status));
    if (status == STATUS::UNKNOWN) {
        feedback(track, "dislike", startRequestTs);
        sync(startRequestTs);
    } else if (status == STATUS::LIKED) {
        feedback(track, "unlike", startRequestTs);
        sync(startRequestTs);
    }
}

void YandexMusic::setCurrentProgress(int position, int duration) {
    currentDurationSec_ = duration;
    currentPositionSec_ = position;
}

void YandexMusic::setCurrentPosition(int position) {
    currentPositionSec_ = position;
}

unsigned int YandexMusic::getCurrentPosition() const {
    return currentPositionSec_;
}

unsigned int YandexMusic::getCurrentDuration() const {
    return currentDurationSec_;
}

Player::ChangeConfigResult YandexMusic::updateConfig(const Json::Value& customYandexMusicConfig) {
    Player::ChangeConfigResult changeConfigResult = Player::ChangeConfigResult::NO_CHANGES;
    changeConfigResult = std::max(changeConfigResult, updateWebsocketSettings(customYandexMusicConfig));
    changeConfigResult = std::max(changeConfigResult, updateWebsocketLogChannels(customYandexMusicConfig));
    changeConfigResult = std::max(changeConfigResult, updateConnectionSettings(customYandexMusicConfig));
    changeConfigResult = std::max(changeConfigResult, updatePingSettings(customYandexMusicConfig));
    changeConfigResult = std::max(changeConfigResult, updateBitRateSettings(customYandexMusicConfig));

    normalizationTargetLufs_ = tryGetDouble(customYandexMusicConfig, "normalizationTargetLufs", Track::Normalization::DEFAULT_TARGET_LUFS);

    auto forcedTrackUrl = tryGetString(customYandexMusicConfig, "forcedTrackUrl", "");
    if (forcedTrackUrl != forcedTrackUrl_) {
        return Player::ChangeConfigResult::NEED_RECREATE;
    }

    return changeConfigResult;
}

Player::ChangeConfigResult YandexMusic::updateBitRateSettings(const Json::Value& config) {
    const bool lowBitrate = tryGetBool(config, "lowBitrate", false);
    const bool oldValue = std::exchange(requestLowBitrate_, lowBitrate);
    if (oldValue != requestLowBitrate_) {
        return Player::ChangeConfigResult::CHANGED;
    }
    return Player::ChangeConfigResult::NO_CHANGES;
}

Player::ChangeConfigResult YandexMusic::updateWebsocketSettings(const Json::Value& customYandexMusicConfig) const {
    std::string currentUrl = (customApiUrl_ ? wsSettings_.url : std::string());
    std::string customUrl = tryGetString(customYandexMusicConfig, FIELD_API_URL);

    bool changed = false;

    if (customUrl != currentUrl) {
        YIO_LOG_INFO("Update YandexMusic url: " << (customUrl.empty() ? "use default" : customUrl) << ". Need recreate object");
        changed = true;
    }

    const int connectTimeoutMs = tryGetInt(customYandexMusicConfig, "connectTimeoutMs", CONNECT_TIMEOUT_MS);
    if (connectTimeoutMs != wsSettings_.connectTimeoutMs) {
        YIO_LOG_INFO("Update YandexMusic connect timeout: " << connectTimeoutMs << ". Need recreate object");
        changed = true;
    }

    if (!changed) {
        YIO_LOG_DEBUG("Update YandexMusic websocket settings: no changes");
    }

    return changed
               ? Player::ChangeConfigResult::NEED_RECREATE
               : Player::ChangeConfigResult::NO_CHANGES;
}

Player::ChangeConfigResult YandexMusic::updateWebsocketLogChannels(const Json::Value& customYandexMusicConfig) {
    const Json::Value& channelNames = tryGetArray(customYandexMusicConfig, "websocketLogs", Json::Value::null);
    return websocketClient_.setLogChannels(channelNames)
               ? Player::ChangeConfigResult::CHANGED
               : Player::ChangeConfigResult::NO_CHANGES;
}

Player::ChangeConfigResult YandexMusic::updatePingSettings(const Json::Value& customYandexMusicConfig) {
    bool changed = false;
    const int pingIntervalSec = tryGetInt(customYandexMusicConfig, "pingIntervalSec", PING_INTERVAL_SEC);
    if (pingIntervalSec != pingIntervalSec_ && pingIntervalSec > 0) {
        pingIntervalSec_ = pingIntervalSec;
        YIO_LOG_INFO("Update YandexMusic ping interval: " << pingIntervalSec);
        changed = true;
    }

    const bool shouldRetryPing = tryGetBool(customYandexMusicConfig, "shouldRetryPing", SHOULD_RETRY_PING);
    if (shouldRetryPing != shouldRetryPing_) {
        YIO_LOG_INFO("Update YandexMusic should retry ping: " << shouldRetryPing);
        shouldRetryPing_ = shouldRetryPing;
        changed = true;
    }

    if (!changed) {
        YIO_LOG_DEBUG("Update YandexMusic ping settings: no changes");
    }

    return changed
               ? Player::ChangeConfigResult::CHANGED
               : Player::ChangeConfigResult::NO_CHANGES;
}

Player::ChangeConfigResult YandexMusic::updateConnectionSettings(const Json::Value& customYandexMusicConfig) {
    bool changed = false;
    const int retriesCount = tryGetInt(customYandexMusicConfig, "retriesCount", RETRIES_COUNT);
    if (retriesCount != retriesCount_) {
        YIO_LOG_INFO("Update YandexMusic retries count: " << retriesCount);
        retriesCount_ = retriesCount;
        changed = true;
    }

    const int retriesCountBeforeReconnect = tryGetInt(customYandexMusicConfig, "retriesCountBeforeReconnect",
                                                      RETRIES_COUNT_BEFORE_RECONNECT);
    if (retriesCountBeforeReconnect != retriesCountBeforeReconnect_) {
        YIO_LOG_INFO("Update YandexMusic retries before reconnect: " << retriesCountBeforeReconnect);
        retriesCountBeforeReconnect_ = retriesCountBeforeReconnect;
        changed = true;
    }

    const int wsRequestTimeoutMs = tryGetInt(customYandexMusicConfig, "requestTimeoutMs",
                                             WS_REQUEST_TIMEOUT_MS);
    if (wsRequestTimeoutMs != wsRequestTimeoutMs_.count()) {
        YIO_LOG_INFO("Update YandexMusic websocket request timeout: " << wsRequestTimeoutMs);
        wsRequestTimeoutMs_ = std::chrono::milliseconds(wsRequestTimeoutMs);
        changed = true;
    }

    const int totalRequestTimeoutMs = tryGetInt(customYandexMusicConfig, "totalRequestTimeoutMs",
                                                TOTAL_REQUEST_TIMEOUT_MS);
    if (totalRequestTimeoutMs != totalRequestTimeoutMs_.count()) {
        YIO_LOG_INFO("Update YandexMusic total request timeout: " << totalRequestTimeoutMs);
        totalRequestTimeoutMs_ = std::chrono::milliseconds(totalRequestTimeoutMs);
        changed = true;
    }

    const bool generateRetryId = tryGetBool(customYandexMusicConfig, "generateRetryId", GENERATE_RETRY_ID);
    if (generateRetryId != generateRetryId_) {
        YIO_LOG_INFO("Update YandexMusic generateRetryId: " << generateRetryId);
        generateRetryId_ = generateRetryId;
        changed = true;
    }

    const int promisesLeakLimit = tryGetInt(customYandexMusicConfig, "promisesLeakLimit", PROMISES_LEAK_LIMIT);
    if (promisesLeakLimit != promisesLeakLimit_) {
        YIO_LOG_INFO("Update YandexMusic promises leak limit: " << promisesLeakLimit);
        promisesLeakLimit_ = promisesLeakLimit;
        changed = true;
    }

    auto connectionRetryConfig = tryGetJson(customYandexMusicConfig, "connectionRetry", Json::Value());
    if (updateRetryDelayCounter(connectionDelayCounter_, connectionRetryConfig)) {
        YIO_LOG_INFO("Update YandexMusic connection delay counter");
        changed = true;
    }

    if (!changed) {
        YIO_LOG_DEBUG("Update YandexMusic connection settings: no changes");
    }

    return changed
               ? Player::ChangeConfigResult::CHANGED
               : Player::ChangeConfigResult::NO_CHANGES;
}

void YandexMusic::pingLoop() {
    while (!stopped_) {
        try {
            ping();
        } catch (const RequestTimeoutException& e) {
            YIO_LOG_WARN("Ping request timeout. Disconnecting..." << e.what());
            disconnect();
        }
        std::unique_lock<std::mutex> lock(pingMutex_);
        pingCondVar_.wait_for(lock, std::chrono::seconds(pingIntervalSec_), [&]() { return stopped_.load(); });
    }
}

void YandexMusic::ping() {
    Json::Value json;
    json["action"] = "ping";
    auto startRequestTs = std::chrono::steady_clock::now();
    request(json, startRequestTs);
}

void YandexMusic::setAuthData(const std::string& uid, const std::string& sessionId, const std::string& token, const std::string& deviceId) {
    std::lock_guard connectionLock(connectionMutex_);
    /* uid change is used by biometry (if uid changes -> re-auth, so YandexMusic will count Device Owner likes and
     * dislikes properly
     */
    bool authDataChanged = token != token_ || uid != uid_;
    if (authDataChanged) {
        isAuthorized_ = false;
    }
    token_ = token;
    uid_ = uid;
    deviceId_ = deviceId;
    sessionId_ = sessionId;
    connectionDelayCounter_.reset();
    connectionCondVar_.notify_all();
}

bool YandexMusic::auth(const std::chrono::steady_clock::time_point& startRequestTs) {
    Json::Value json;
    Json::Value data;

    data["owner"] = token_;
    data["user"] = uid_;
    data["devId"] = deviceId_;

    json["action"] = "auth";
    json["data"] = data;

    auto latencyPoint = device_->telemetry()->createLatencyPoint();
    bool result = !(request(json, startRequestTs).empty());
    device_->telemetry()->reportLatency(latencyPoint, "musicPlayerAuthEnd");

    return result;
}

void YandexMusic::sync(const std::chrono::steady_clock::time_point& startRequestTs) {
    std::lock_guard<std::mutex> guard(playlistMutex_);

    Json::Value json;
    Json::Value data;
    Json::Value queue = Json::arrayValue;

    // steps to make sync
    // 1. take current playlist
    for (const auto& track : playlist_) {
        queue.append(trackToJson(track));
    }

    data["queue"] = queue;
    // 2. take current track index and sessionId
    data["index"] = index_;
    data["sessionId"] = sessionId_;

    json["action"] = "sync";
    json["data"] = data;

    // 3. send it
    auto latencyPoint = device_->telemetry()->createLatencyPoint();
    const Json::Value& response = request(json, startRequestTs);
    needSync_ = false;
    // 4. take response from server
    device_->telemetry()->reportLatency(latencyPoint, "musicPlayerSyncEnd");

    if (response.empty()) {
        YIO_LOG_ERROR_EVENT("YandexMusic.FailedSync.EmptyResponse", "YandexMusic sync empty response");
        return;
    }

    YIO_LOG_DEBUG("Got sync: " << response["reqId"].asString() << " with result: " << jsonToString(response["result"]));
    const Json::Value& responseData = response["data"];
    // 5. remove some tracks from start and end
    int removeCountAtStart = responseData["remove"][0].asInt();
    int removeCountAtEnd = responseData["remove"][1].asInt();
    playlist_.erase(playlist_.begin(), playlist_.begin() + removeCountAtStart);
    playlist_.erase(playlist_.end() - removeCountAtEnd, playlist_.end());

    if (responseData.isMember("streamInfo")) {
        const auto& streamInfo = responseData["streamInfo"];
        playlistInfo_.id = streamInfo["itemId"].asString();
        playlistInfo_.type = streamInfo["itemType"].asString();
        playlistInfo_.description = streamInfo["description"].asString();
        if (streamInfo.isMember("shuffled")) {
            playlistInfo_.shuffled = streamInfo["shuffled"].asBool();
        }
        if (streamInfo.isMember("repeatMode")) {
            playlistInfo_.repeatMode = streamInfo["repeatMode"].asString();
        }
    } else {
        playlistInfo_ = {};
    }

    // 6. add new tracks to the end of playlist
    YIO_LOG_TRACE("parseAdditionsStart");
    const Json::Value& additions = responseData["add"];

    YIO_LOG_INFO("sync remove from start " << removeCountAtStart
                                           << ", remove from end " << removeCountAtEnd << ", add to end " << additions.size());
    for (Json::Value::ArrayIndex i = 0; i < additions.size(); i++) {
        const Json::Value& addition = additions[i];

        YIO_LOG_TRACE("parseAdditionsStepStart");
        std::shared_ptr<Track> track = std::make_shared<Track>();

        // create filler track for autostop
        if (addition["command"] == "stop") {
            track->id = "stop";
        } else {
            track->id = addition["id"].asString();
            track->batchId = addition["batchId"].asString();
            track->batchInfo = addition["batchInfo"];
            track->status = STATUS::UNKNOWN;
            track->fullJsonInfo = addition;

            track->coverUri = addition["coverUri"].asString();
            track->durationMs = addition["durationMs"].asDouble();
            track->title = addition["title"].asString();
            track->type = addition["type"].asString();
            try {
                track->artists = parseArtists(addition["artists"]);
            } catch (const Json::Exception& e) {
                YIO_LOG_ERROR_EVENT("YandexMusic.BadJson.Artists", "Can't parse artists: " << e.what());
            }
            try {
                track->albumGenre = parseGenreFromAlbums(addition["albums"]);
            } catch (const Json::Exception& e) {
                YIO_LOG_ERROR_EVENT("YandexMusic.BadJson.Albums", "Can't parse album genres: " << e.what());
            }

            YIO_LOG_TRACE("checkAliceShotPresense");
            if (addition.isMember("shotInfo")) {
                YIO_LOG_TRACE("found short, add it to track");
                const Json::Value& shotInfo = addition["shotInfo"];
                track->shotUrl = shotInfo["audio"].asString();
                track->shotId = shotInfo["id"].asString();
                track->shotDurationMs = shotInfo["durationMs"].asDouble();
            }
        }

        playlist_.push_back(track);
        YIO_LOG_TRACE("parseAdditionsStepEnd");
    }
    // 7. set current track index
    YIO_LOG_INFO("sync setting index to: " << data["index"].asString() << ", playlist size: " << playlist_.size());
    index_ = responseData["index"].asInt();
    YIO_LOG_TRACE("sync handled");
}

void YandexMusic::checkForShot(bool isInitial) {
    std::lock_guard<std::mutex> guard(playlistMutex_);
    if (playlist_.empty()) {
        YIO_LOG_DEBUG("Shot: there are no tracks in playlist, exiting");
        shot_ = nullptr;
        return;
    }

    if (index_ < 0 || (unsigned int)index_ >= playlist_.size()) {
        YIO_LOG_ERROR_EVENT("YandexMusic.InvalidShotIndex", "Shot: wrong index: " << index_ << ", playlist size: " << playlist_.size());
        shot_ = nullptr;
        return;
    }

    std::shared_ptr<Track> track = playlist_[index_];
    if (!shot_) {
        if (track && !track->shotUrl.empty() && !isInitial) {
            YIO_LOG_DEBUG("Shot: no shots played yet or restart, but we have one for current track, use it");
            shot_ = shotify(track);
        }
    } else {
        YIO_LOG_DEBUG("Shot: shot is already played, next track should be normal");
        shot_ = nullptr;
    }
}

std::string YandexMusic::parseArtists(const Json::Value& artists) {
    std::stringstream ss;
    if (artists.isArray() && !artists.empty()) {
        /* Get first artist name because current UI can draw only one artist */
        for (Json::ArrayIndex i = 0; i < artists.size(); ++i) {
            const Json::Value& artist = artists[i];
            /* Set artist name anyway: it should be there */
            ss << artist["name"].asString();
            /* Decomposed is a stuff about to same whether comma or "ft", ... etc */
            if (artist.isMember("decomposed")) {
                /* it should be array -> otherwise error -> skip it */
                if (artist["decomposed"].isArray()) {
                    for (Json::ArrayIndex j = 0; j < artist["decomposed"].size(); ++j) {
                        if (artist["decomposed"][j].isString()) {
                            ss << artist["decomposed"][j].asString();
                        } else if (artist["decomposed"][j].isMember("name")) {
                            /* Decomposed artist has name -> set up */
                            ss << artist["decomposed"][j]["name"].asString();
                        }
                    }
                }
            }
            /* set up comma if not last */
            if (i != artists.size() - 1) {
                ss << ", ";
            }
        }
    }
    return ss.str();
}

std::string YandexMusic::parseGenreFromAlbums(const Json::Value& albums) {
    // TODO: Может ли так случиться что у трека несколько альбомов с разными жанрами? Возможно, понадобится более умная логика
    if (albums.isArray()) {
        for (Json::ArrayIndex i = 0; i < albums.size(); ++i) {
            const Json::Value& album = albums[i];
            if (album.isMember("genre")) {
                const Json::Value& genre = album["genre"];
                if (genre.isString()) {
                    return genre.asString();
                }
            }
        }
    }
    return "";
}

void YandexMusic::requestUrlForTrack(Track& track, const std::chrono::steady_clock::time_point& startRequestTs) {
    if (!forcedTrackUrl_.empty()) {
        track.url = forcedTrackUrl_;
        return;
    }

    if (track.isShot) {
        // nothing to request
        return;
    }

    Json::Value json;
    Json::Value data;
    data["track"] = trackToJson(track);
    data["lowBitrate"] = requestLowBitrate_;
    json["action"] = "url";
    json["data"] = data;

    auto latencyPoint = device_->telemetry()->createLatencyPoint();
    const Json::Value& response = request(json, startRequestTs);
    device_->telemetry()->reportLatency(latencyPoint, "musicPlayerRequestUrlEnd");

    const Json::Value& responseData = response["data"];
    track.url = tryGetString(responseData, "url", "");
    track.normalization = extractNormalization(responseData, normalizationTargetLufs_.load());
}

void YandexMusic::feedback(const std::shared_ptr<Track>& track, const std::string& type,
                           const std::chrono::steady_clock::time_point& startRequestTs) {
    if (!track) {
        YIO_LOG_ERROR_EVENT("YandexMusic.FailedFeedback.NullTrack", "YandexMusic cannot feedback on null track " << type);
        return;
    }
    if (track->isShot) {
        // this is expected behavior. just return
        return;
    }

    if (needSync_) {
        sync(startRequestTs);
    }

    Json::Value json;
    Json::Value data;
    Json::Value timings;

    timings["played"] = currentPositionSec_;
    timings["position"] = currentPositionSec_;
    timings["duration"] = currentDurationSec_;

    data["type"] = type;
    data["track"] = trackToJson(track);
    data["playId"] = track->id + ":" + track->batchId;
    data["timings"] = timings;

    json["action"] = "feedback";
    json["data"] = data;

    auto latencyPoint = device_->telemetry()->createLatencyPoint();
    request(json, startRequestTs);
    device_->telemetry()->reportLatency(latencyPoint, "musicPlayerFeedbackEnd");
}

Json::Value YandexMusic::request(Json::Value& json, const std::chrono::steady_clock::time_point& startRequestTs) {
    int64_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    json["ts"] = std::to_string(ts);

    const std::string action = json["action"].asString();
    std::string reqId = action + "-" + makeUUID();

    for (int i = 0; i < retriesCount_; i++) {
        if (i > 0 && generateRetryId_) {
            reqId = action + "-" + makeUUID();
        }

        Json::Value result = requestTry(json, reqId, action, i, startRequestTs);
        if (!result.isNull()) {
            return result;
        }

        YIO_LOG_DEBUG("Will retry request: " << reqId);
        if (i + 1 == retriesCountBeforeReconnect_) {
            Json::Value event;
            event["action"] = action;
            device_->telemetry()->reportEvent("musicPlayerClientReconnect", jsonToString(event));
            YIO_LOG_INFO("disconnect for " << action << " in " << i << " try");
            disconnect();

            // auth will be sent again during reconnection
            if (action == "auth") {
                return Json::objectValue;
            }
        }
    }

    Json::Value event;
    event["action"] = action;
    device_->telemetry()->reportEvent("musicPlayerClientDisconnect", jsonToString(event));

    disconnect();
    YIO_LOG_ERROR_EVENT("YandexMusic.RequestTimeout", "request timeout, connection was lost in " << reqId);
    return Json::objectValue;
}

Json::Value YandexMusic::requestTry(Json::Value& json, const std::string& requestId, const std::string& action,
                                    const int tryCount, const std::chrono::steady_clock::time_point& startRequestTs) {
    json["reqId"] = requestId;
    json["retry"] = tryCount;

    // if retrying
    if (tryCount > 0) {
        if (action == "ping" && !shouldRetryPing_) {
            return Json::objectValue;
        }
        Json::Value eventJson;
        eventJson["action"] = action;
        eventJson["retry"] = tryCount;
        device_->telemetry()->reportEvent("musicPlayerRetry", jsonToString(eventJson));
    }

    // need to check if stopped before all potentially long operations
    if (stopped_) {
        YIO_LOG_INFO("request interrupted before waitUntilConnected " << requestId);
        return Json::objectValue;
    }

    // Action "auth" comes only from connectionThread_, but waitUntilConnected MUST NOT be called
    // from connectionThread_, because it leads to deadlock.
    if (action != "auth") {
        waitUntilConnected(totalRequestTimeoutMs_ - getMillisSince(startRequestTs));
        checkRequestTimeout(action, startRequestTs);
    } else if (!isConnected_) {
        YIO_LOG_INFO("websocket was disconnected during auth request " << requestId);
        return Json::objectValue;
    }

    if (!UNAUTHORIZED_ACTIONS.count(action)) {
        waitUntilAuthorized(totalRequestTimeoutMs_ - getMillisSince(startRequestTs));
        checkRequestTimeout(action, startRequestTs);
    }

    if (action != "ping" || tryCount > 0) {
        if (action == "auth") {
            Json::Value jsonCopy = json;
            jsonCopy["data"]["owner"] = "DON'T LOG ME";
            YIO_LOG_DEBUG("auth: " << jsonToString(jsonCopy));
        } else {
            YIO_LOG_INFO(action << ": " << jsonToString(json));
        }
    }

    // need to check if stopped before all potentially long operations
    if (stopped_) {
        YIO_LOG_INFO("request interrupted before new promise " << requestId);
        return Json::objectValue;
    }

    std::future<Json::Value> f;
    {
        std::unique_lock lock(requestMutex_);
        requestPromises_[requestId] = std::make_unique<std::promise<Json::Value>>();
        f = requestPromises_[requestId]->get_future();
    }

    // will clean itself before returning back from method
    Y_DEFER {
        std::unique_lock lock(requestMutex_);
        requestPromises_.erase(requestId);

        int pendingPromises = requestPromises_.size();
        YIO_LOG_TRACE("request " << requestId << " erased. Map size: " << pendingPromises);
        if (pendingPromises >= promisesLeakLimit_) {
            Json::Value eventJson;
            eventJson["value"] = pendingPromises;
            device_->telemetry()->reportEvent("musicPlayerPromiseLeakDetected", jsonToString(eventJson));
        }
    };

    websocketClient_.unsafeSend(jsonToString(json));

    auto latencyPoint = measureRequestStart(action);
    auto waitResult = f.wait_for(std::min(
        wsRequestTimeoutMs_,
        totalRequestTimeoutMs_ - getMillisSince(startRequestTs)));
    measureRequestFinish(action, latencyPoint);

    checkRequestTimeout(action, startRequestTs);

    if (stopped_) {
        YIO_LOG_WARN("request interrupted: " << requestId);
        return Json::objectValue;
    }

    if (waitResult == std::future_status::ready) {
        Json::Value response = f.get();
        if (response["result"]["success"].asBool()) {
            return response;
        } else {
            YIO_LOG_INFO("response failure received for " << requestId);
        }

        // Don't need to retry "auth" action here, because retry is properly
        // handled in caller method (YandexMusic::tryAuth)
        if (!response["result"]["canRetry"].asBool() || action == "auth") {
            return Json::objectValue;
        }
    }

    // return null value as sign to try again
    return Json::nullValue;
}

std::shared_ptr<const YandexIO::LatencyData> YandexMusic::measureRequestStart(const std::string& action) {
    if (action == "ping") {
        return nullptr;
    }
    return device_->telemetry()->createLatencyPoint();
}

void YandexMusic::measureRequestFinish(const std::string& action, std::shared_ptr<const YandexIO::LatencyData>& latencyPoint) {
    if (action == "ping") {
        return;
    }

    const std::string latencyPointEvent = "musicPlayerWebsocketWaitingResultEnd_" + action;
    device_->telemetry()->reportLatency(latencyPoint, latencyPointEvent);
}

void YandexMusic::checkRequestTimeout(const std::string& action, const std::chrono::time_point<std::chrono::steady_clock>& startTimestamp) {
    auto requestDuration = getMillisSince(startTimestamp);
    if (requestDuration >= totalRequestTimeoutMs_) {
        std::stringstream what;
        what << "Request timeout exceeded for " << action << ". Timeout is " << totalRequestTimeoutMs_.count()
             << "ms, but request took " << requestDuration.count() << "ms.";
        throw RequestTimeoutException(what.str());
    }
}

void YandexMusic::onConnect() {
    device_->telemetry()->reportLatency(std::move(playerReconnectLatencyPoint_), "musicPlayerReconnect");
}

void YandexMusic::onMessage(const std::string& msg) {
    std::lock_guard lock(onMessageMutex_);
    const auto response = tryParseJson(msg);
    if (!response.has_value()) {
        YIO_LOG_ERROR_EVENT("YandexMusic.BadJson.Message", "onMessage parse error: " + msg);
        Json::Value event;
        event["message"] = msg;
        device_->telemetry()->reportEvent("musicPlayerBackendParseError", jsonToString(event));
        return;
    }

    const Json::Value& responseValue = response.value();
    const std::string reqId = responseValue["reqId"].asString();
    if (!responseValue["result"]["success"].asBool()) {
        YIO_LOG_ERROR_EVENT("YandexMusic.BackendError", "onUnsuccesfullMessage: " + msg);
        Json::Value event;
        event["message"] = msg;
        device_->telemetry()->reportEvent("musicPlayerBackendErrorMessage", jsonToString(event));

        if (tryGetString(responseValue, "data") == "Authentication failure") {
            std::scoped_lock lock(requestMutex_);
            if (auto reqIt = requestPromises_.find(reqId); reqIt != requestPromises_.end()) {
                reqIt->second->set_exception(std::make_exception_ptr(
                    AuthorizationFailedException("Backend returned 'Authentication failure' error.")));
                requestPromises_.erase(reqIt);
            }
        }
    } else if (reqId.starts_with("url")) {
        YIO_LOG_INFO("onMessage: " + msg);
    }

    std::unique_lock requestLock(requestMutex_);
    if (requestPromises_.count(reqId)) {
        requestPromises_[reqId]->set_value(responseValue);
        /* Delete promise, so other thread (destructor) won't be able to satisfy promise again */
        requestPromises_.erase(reqId);
    } else {
        YIO_LOG_WARN("unexpected server request: " + jsonToString(responseValue));
    }
}

void YandexMusic::onDisconnect(const Websocket::ConnectionInfo& connectionInfo) {
    YIO_LOG_DEBUG("onDisconnect");
    Json::Value event;
    event["message"] = connectionInfo.toString();
    device_->telemetry()->reportEvent("musicPlayerServerDisconnect", jsonToString(event));

    std::lock_guard lock(connectionMutex_);
    isConnected_ = false;
    isAuthorized_ = false;
    needSync_ = true;
    connectionCondVar_.notify_all();
    playerReconnectLatencyPoint_ = device_->telemetry()->createLatencyPoint();
}

void YandexMusic::onFail(const Websocket::ConnectionInfo& connectionInfo) {
    YIO_LOG_INFO("onFail");
    Json::Value event;
    event["message"] = connectionInfo.toString();
    device_->telemetry()->reportEvent("musicPlayerConnectionFail", jsonToString(event));

    std::lock_guard lock(connectionMutex_);
    isConnected_ = false;
    isAuthorized_ = false;
    needSync_ = true;
    connectionCondVar_.notify_all();
}

void YandexMusic::waitUntilAuthorized(std::chrono::milliseconds timeout) {
    YIO_LOG_TRACE("YandexMusic::waitUntilAuthorized");
    if (std::this_thread::get_id() == connectionThread_.get_id()) {
        throw std::logic_error("waitUntilAuthorized was called from connectionThread_");
    }
    if (!isAuthorized_) {
        std::unique_lock lock(connectionMutex_);
        if (!isAuthorized_) {
            YIO_LOG_DEBUG("YandexMusic::waitUntilAuthorized, start wait");
            connectionCondVar_.wait_for(lock, timeout, [this]() { return isAuthorized_ || stopped_; });
            YIO_LOG_DEBUG("YandexMusic::waitUntilAuthorized, finish wait");
        }
    }
}

void YandexMusic::waitUntilConnected(std::chrono::milliseconds timeout) {
    YIO_LOG_TRACE("YandexMusic::waitUntilConnected");
    if (std::this_thread::get_id() == connectionThread_.get_id()) {
        throw std::logic_error("waitUntilConnected was called from connectionThread_");
    }
    if (!isConnected_) {
        std::unique_lock lock(connectionMutex_);
        if (!isConnected_) {
            YIO_LOG_DEBUG("YandexMusic::waitUntilConnected, start wait");
            connectionCondVar_.wait_for(lock, timeout, [this]() { return isConnected_ || stopped_; });
            YIO_LOG_DEBUG("YandexMusic::waitUntilConnected, finish wait");
        }
    }
}

void YandexMusic::connectionLoop() {
    YIO_LOG_TRACE("YandexMusic::connectionLoop");

    while (!stopped_) {
        tryConnect();
        tryAuth();
        std::unique_lock lock(connectionMutex_);
        connectionCondVar_.wait(lock, [this]() {
            bool needAuth = !isAuthorized_ && !token_.empty();
            return stopped_ || !isConnected_ || needAuth;
        });
    }
}

void YandexMusic::tryConnect() {
    YIO_LOG_TRACE("YandexMusic::tryConnect");
    if (!isConnected_ && !stopped_) {
        std::unique_lock lock(connectionMutex_);
        while (!isConnected_ && !stopped_) {
            auto latencyPoint = device_->telemetry()->createLatencyPoint();
            YIO_LOG_DEBUG("YandexMusic::tryConnect: call connectSyncWithTimeout()");
            isConnected_ = websocketClient_.connectSyncWithTimeout(wsSettings_);
            YIO_LOG_DEBUG("YandexMusic::tryConnect: isConnected_=" << isConnected_);
            device_->telemetry()->reportLatency(latencyPoint,
                                                isConnected_ ? "musicPlayerWebsocketConnectingEnd" : "musicPlayerConnectTimeout");
            device_->telemetry()->reportEvent(
                isConnected_ ? "musicPlayerConnected" : "musicPlayerConnectTimeout");
            if (!isConnected_) {
                connectionCondVar_.wait_for(lock, connectionDelayCounter_.get(), [this]() { return stopped_.load(); });
                connectionDelayCounter_.increase();
            }
        }
        connectionCondVar_.notify_all();
    }
}

void YandexMusic::tryAuth() {
    YIO_LOG_TRACE("YandexMusic::tryAuth");
    if (isConnected_ && !isAuthorized_ && !stopped_) {
        std::unique_lock lock(connectionMutex_);
        int tryNumber = 0;
        while (isConnected_ && !isAuthorized_ && !stopped_) {
            if (token_.empty()) {
                YIO_LOG_INFO("Auth token is empty; skip auth.");
                device_->telemetry()->reportEvent("musicPlayerNoAuthData");
                break;
            }

            lock.unlock(); // unlock mutex during request to avoid deadlocks

            tryNumber = (tryNumber + 1) % retriesCountBeforeReconnect_;
            if (tryNumber == 0) {
                disconnect();
                tryConnect();
            }

            bool authResult = false;
            bool isAuthFailed = false;
            try {
                auto startRequestTs = std::chrono::steady_clock::now();
                authResult = auth(startRequestTs);
            } catch (const AuthorizationFailedException& e) {
                isAuthFailed = true;
                YIO_LOG_WARN("Auth failed exception. " << e.what());
            } catch (const RequestTimeoutException& e) {
                YIO_LOG_WARN("Auth request timeout. " << e.what());
            }
            lock.lock();

            isAuthorized_ = authResult;
            device_->telemetry()->reportEvent(
                isAuthorized_ ? "musicPlayerAuthorized" : "musicPlayerUnauthorized");

            if (isAuthFailed && onAuthFailedHandler_) {
                onAuthFailedHandler_();
            }

            if (!isAuthorized_) {
                connectionCondVar_.wait_for(lock, connectionDelayCounter_.get(), [this]() { return stopped_.load(); });
                connectionDelayCounter_.increase();
            } else {
                needSync_ = true;
                connectionDelayCounter_.reset();
            }
        }
        connectionCondVar_.notify_all();
    }
}

void YandexMusic::disconnect() {
    websocketClient_.interruptWait();
    websocketClient_.disconnectSyncWithTimeout(wsRequestTimeoutMs_.count());

    std::lock_guard lock(connectionMutex_);
    isConnected_ = false;
    isAuthorized_ = false;
    needSync_ = true;
    connectionCondVar_.notify_all();
}

bool YandexMusic::updateRetryDelayCounter(RetryDelayCounter& delayCounter, const Json::Value& config) {
    RetryDelayCounter::Settings newSettings;

    newSettings.init = std::chrono::milliseconds(tryGetInt(config, "firstDelayMs", DEFAULT_CONNECTION_FIRST_DELAY_MS));
    newSettings.max = std::chrono::milliseconds(tryGetInt(config, "maxDelayMs", DEFAULT_CONNECTION_MAX_DELAY_MS));
    newSettings.factor = tryGetInt(config, "factor", DEFAULT_CONNECTION_RETRY_DELAY_FACTOR);

    const auto& currentSettings = delayCounter.getSettings();

    bool changed = false;

    if (currentSettings.init != newSettings.init) {
        YIO_LOG_INFO("Updated YandexMusic retry delay counter init: " << newSettings.init.count());
        changed = true;
    }
    if (currentSettings.max != newSettings.max) {
        YIO_LOG_INFO("Updated YandexMusic retry delay counter max: " << newSettings.max.count());
        changed = true;
    }
    if (currentSettings.factor != newSettings.factor) {
        YIO_LOG_INFO("Updated YandexMusic retry delay counter factor: " << newSettings.factor);
        changed = true;
    }

    delayCounter.setSettings(newSettings);
    return changed;
}
