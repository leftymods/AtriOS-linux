#include "yandex_music_player.h"

#include "common.h"

#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/ipc/i_connector.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include "yandex_io/protos/account_storage.pb.h"
#include <yandex_io/protos/quasar_proto.pb.h>

#include <speechkit/SpeechKit.h>
#include <speechkit/UniProxyClientSettings.h>

#include <functional>

YIO_DEFINE_LOG_MODULE("audio_sender");

using namespace quasar;

using namespace AudioSender;

namespace {

    const std::chrono::seconds DEFAULT_IPC_TIMEOUT{3};
    const std::chrono::seconds REQUEST_OAUTH_TIMEOUT = DEFAULT_IPC_TIMEOUT;
    const std::chrono::seconds UNIPROXY_CONNECTION_TIMEOUT{5};
    const std::chrono::seconds REQUEST_SESSION_ID_TIMEOUT{5};
    const std::chrono::seconds FIRST_HEARTBIT_TIMEOUT{10};
    const std::chrono::seconds STOP_MUSIC_TIMEOUT{5};

} // namespace

YandexMusicPlayer::SharedPtr YandexMusicPlayer::create(
    const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory,
    const std::string& uuid,
    const std::string& apiKey,
    const std::string& uniProxyUrl,
    std::shared_ptr<quasar::IAuthProvider> authProvider)
{
    SpeechKit::SpeechKit::getInstance()->setUuid(uuid);
    SpeechKit::SpeechKit::getInstance()->setApiKey(apiKey);
    return std::make_shared<YandexMusicPlayer>(ipcFactory, uniProxyUrl, std::move(authProvider));
}

YandexMusicPlayer::YandexMusicPlayer(
    const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory,
    const std::string& uniProxyUrl,
    std::shared_ptr<quasar::IAuthProvider> authProvider)
    : ipcFactory_(ipcFactory)
    , uniProxyUrl_(uniProxyUrl)
    , authProvider_(std::move(authProvider))
    , callbackQueue_{std::make_shared<NamedCallbackQueue>("AudiosenderYandexMusicPlayer")}
    , uniqueCallback_{callbackQueue_, UniqueCallback::ReplaceType::INSERT_BACK}
    , mediaConnector_{ipcFactory_->createIpcConnector("mediad")}
{
    authProvider_->ownerAuthInfo().connect([this](const auto& authInfo) {
        oauthToken_ = authInfo->authToken;
        if (!oauthToken_.empty() && state_ == State::STARTING_FROM_OAUTH) {
            uniqueCallback_.reset();
            startFromUniproxyInit();
        }
    }, lifetime_, callbackQueue_);
    oauthToken_ = authProvider_->ownerAuthInfo().value()->authToken;

    mediaConnector_->connectToService();
    mediaConnector_->waitUntilConnected();
}

YandexMusicPlayer::~YandexMusicPlayer() {
    uniqueCallback_.reset();
    lifetime_.die();
    callbackQueue_.reset();
}

CommandResult YandexMusicPlayer::play(const std::string& music, int offset) {
    YIO_LOG_DEBUG("music=" << music << "offset=" << offset);

    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    callbackQueue_->add([this, promise{std::move(promise)}, music, offset]() mutable {
        promise_ = std::move(promise);
        playInternal(music, offset);
    });

    const auto timeout = REQUEST_OAUTH_TIMEOUT + UNIPROXY_CONNECTION_TIMEOUT +
                         FIRST_HEARTBIT_TIMEOUT;
    if (future.wait_for(timeout) == std::future_status::timeout) {
        // should not happen
        return {false, "Yandex music player unknown timeout"};
    }

    try {
        future.get();
    } catch (std::runtime_error& err) {
        YIO_LOG_ERROR_EVENT("YandexMusicPlayer.PlayFailed", err.what());
        return {false, err.what()};
    }
    return {};
}

CommandResult YandexMusicPlayer::stop() {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    callbackQueue_->add([this, promise{std::move(promise)}]() mutable {
        promise_ = std::move(promise);

        if (state_ != State::PLAYING) {
            promise_->set_exception(std::make_exception_ptr(
                std::runtime_error("Music player already stopped")));
            return;
        }

        stopInternal();
    });

    if (future.wait_for(STOP_MUSIC_TIMEOUT) == std::future_status::timeout) {
        return {false, "Yandex music player unknown timeout"};
    }

    try {
        future.get();
    } catch (std::runtime_error& err) {
        YIO_LOG_ERROR_EVENT("YandexMusicPlayer.StopFailed", err.what());
        return {false, err.what()};
    }
    return {};
}

void YandexMusicPlayer::onUniProxyProtocolDirective(SpeechKit::UniProxyClient::SharedPtr uniProxyClient,
                                                    const std::string& jsonDirectiveStr)
{
    callbackQueue_->add([this, uniProxyClient{std::move(uniProxyClient)}, jsonDirectiveStr] {
        if (uniProxyClient != uniProxy_ || state_ != State::SESSION_ID_INITIALIZING) {
            return;
        }

        YIO_LOG_DEBUG("onUniProxyProtocolDirective" << jsonDirectiveStr);
        Json::Value jsonDirective = parseJson(jsonDirectiveStr);
        const auto& directives = jsonDirective["payload"]["response"]["directives"];
        for (const auto& directive : directives) {
            if (directive["name"] == "music_play") {
                musicSessionId_ = directive["payload"]["session_id"].asString();
                uniqueCallback_.reset();
                sendPlayToMedia();
                return;
            }
        }

        const auto& metas = jsonDirective["payload"]["response"]["meta"];
        for (const auto& meta : metas) {
            if (meta["type"] == "error" && meta["error_type"] == "unauthorized") {
                uniqueCallback_.reset();
                state_ = State::IDLE;
                promise_->set_exception(std::make_exception_ptr(
                    std::runtime_error("Unauthorized. Maybe Yandex Plus is required")));
                return;
            }
        }
    });
}

void YandexMusicPlayer::onConnectionStateChanged(SpeechKit::UniProxyClient::SharedPtr uniProxyClient, bool isConnected) {
    callbackQueue_->add([this, uniProxyClient{std::move(uniProxyClient)}, isConnected] {
        if (uniProxyClient != uniProxy_) {
            return;
        }

        YIO_LOG_DEBUG("onConnectionStateChanged" << isConnected);

        uniproxyConnected_ = isConnected;
        if (uniproxyConnected_ && state_ == State::STARTING_FROM_UNIPROXY) {
            uniqueCallback_.reset();

            Json::Value payload;
            payload["oauth_token"] = oauthToken_;
            payload["vins"]["application"]["app_id"] = "aliced";

            uniProxy_->sendEvent(
                SpeechKit::UniProxy::Header{"System", "SynchronizeState"}, Json::FastWriter().write(payload));

            initMusicSessionId();
        }
    });
}
void YandexMusicPlayer::onUniProxyProtocolError(SpeechKit::UniProxyClient::SharedPtr /* uniProxyClient */,
                                                const SpeechKit::Error& error)
{
    YIO_LOG_ERROR_EVENT("YandexMusicPlayer.UniproxyError", error.getString());
}

void YandexMusicPlayer::playInternal(const std::string& music, int offset) {
    music_ = music;
    offset_ = offset;

    if (oauthToken_.empty()) {
        startFromOauthInit();
        return;
    }

    if (!uniproxyConnected_) {
        startFromUniproxyInit();
        return;
    }

    initMusicSessionId();
}

void YandexMusicPlayer::stopInternal() {
    YIO_LOG_DEBUG("Preparing stop music message to mediad");

    state_ = State::STOPPING;

    proto::QuasarMessage message;
    const auto mediaRequest = message.mutable_media_request();
    mediaRequest->mutable_pause();

    if (!mediaConnector_->sendMessage(std::move(message))) {
        state_ = State::PLAYING;
        promise_->set_exception(std::make_exception_ptr(
            std::runtime_error("Cannot connect to mediad")));
        return;
    }

    setTimeoutCallback(STOP_MUSIC_TIMEOUT, "Stop music timeout");
}

void YandexMusicPlayer::startFromOauthInit() {
    YIO_LOG_DEBUG("OAuth token initialization");
    state_ = State::STARTING_FROM_OAUTH;

    setTimeoutCallback(REQUEST_OAUTH_TIMEOUT, "Auth response timeout");
}

void YandexMusicPlayer::startFromUniproxyInit() {
    YIO_LOG_DEBUG("UniProxy initialization");

    state_ = State::STARTING_FROM_UNIPROXY;

    SpeechKit::UniProxyClientSettings upSettings{};
    upSettings.oauthToken = oauthToken_;
    upSettings.uniProxyUrl = uniProxyUrl_;
    uniProxy_ = SpeechKit::UniProxyClient::create(upSettings, shared_from_this());

    uniProxy_->start();

    setTimeoutCallback(UNIPROXY_CONNECTION_TIMEOUT, "Uniproxy connection timed out");
}

void YandexMusicPlayer::initMusicSessionId() {
    YIO_LOG_DEBUG("Music session ID initialization");

    state_ = State::SESSION_ID_INITIALIZING;

    Json::Value payload;
    payload["header"]["request_id"] = makeUUID();
    payload["request"]["voice_session"] = false;
    payload["request"]["event"]["type"] = "text_input";

    if (music_.empty() || music_ == "*") {
        payload["request"]["event"]["text"] = "включи музыку";
    } else {
        payload["request"]["event"]["text"] = "включи " + music_;
    }
    payload["application"]["client_time"] = formatLocalTime("%Y%m%dT%H%M%S");
    payload["application"]["lang"] = "ru-RU";
    payload["application"]["timestamp"] = formatUnixTimestamp();
    payload["application"]["timezone"] = "";

    uniProxy_->sendEvent(SpeechKit::UniProxy::Header{"Vins", "TextInput"}, Json::FastWriter().write(payload));

    setTimeoutCallback(REQUEST_SESSION_ID_TIMEOUT, "Response with session_id timed out");
}

void YandexMusicPlayer::sendPlayToMedia() {
    YIO_LOG_DEBUG("Preparing play music message to mediad");

    state_ = State::WAITING_FIRST_HEARTBIT;

    proto::QuasarMessage message;
    auto mediaRequest = message.mutable_media_request();
    mediaRequest->set_session_id(TString(musicSessionId_));
    mediaRequest->set_uid("");
    mediaRequest->set_offset(offset_);
    mediaRequest->mutable_play_audio();

    if (!mediaConnector_->sendMessage(std::move(message))) {
        state_ = State::IDLE;
        promise_->set_exception(std::make_exception_ptr(
            std::runtime_error("Cannot connect to mediad")));
        return;
    }

    setTimeoutCallback(FIRST_HEARTBIT_TIMEOUT, "First heartbit timeout");
}

void YandexMusicPlayer::onMusicState(const quasar::proto::AppState::MusicState& musicState) {
    callbackQueue_->add([this, musicState] {
        if (state_ == State::WAITING_FIRST_HEARTBIT) {
            if (musicState.has_progress() && musicState.progress() > 0) {
                uniqueCallback_.reset();
                state_ = State::PLAYING;
                promise_->set_value();
            }
        } else if (state_ == State::STOPPING || state_ == State::PLAYING) {
            if (musicState.has_is_paused() && musicState.is_paused()) {
                if (state_ == State::STOPPING) {
                    uniqueCallback_.reset();
                    promise_->set_value();
                }
                state_ = State::IDLE;
            }
        }
    });
}

void YandexMusicPlayer::setTimeoutCallback(const std::chrono::seconds& timeout, const std::string& error) {
    uniqueCallback_.executeDelayed([this, error] {
        YIO_LOG_ERROR_EVENT("YandexMusicPlayer.Timeout", error);
        state_ = State::IDLE;
        promise_->set_exception(std::make_exception_ptr(std::runtime_error(error)));
    }, timeout, lifetime_);
}
