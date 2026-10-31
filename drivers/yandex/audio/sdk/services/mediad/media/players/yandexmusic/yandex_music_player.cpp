#include "yandex_music_player.h"

#include "mp_listener.h"

#include <yandex_io/services/mediad/media/suicide_exception.h>
#include <yandex_io/services/mediad/spectrum_listener/spectrum_listener.h>

#include <yandex_io/libs/audio_player/base/audio_player.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/telemetry/telemetry.h>
#include <yandex_io/libs/protobuf_utils/json.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <exception>
#include <future>
#include <iostream>

YIO_DEFINE_LOG_MODULE("media");

using namespace quasar;

YandexMusicPlayer::YandexMusicPlayer(
    std::shared_ptr<YandexIO::IDevice> device,
    std::shared_ptr<ipc::IIpcFactory> ipcFactory,
    std::shared_ptr<const IAudioClockManager> audioClockManager,
    bool sslVerification, bool ownsFocus,
    std::shared_ptr<AudioPlayerFactory> playerFactory,
    std::shared_ptr<IMultiroomProvider> multiroomProvider,
    std::shared_ptr<IStereoPairProvider> stereoPairProvider,
    std::shared_ptr<IUserConfigProvider> userConfigProvider,
    const Json::Value& customYandexMusicConfig,
    OnStateChange onStateChange,
    OnPlayStartChange onStart,
    OnError onError)
    : Player(std::move(device), std::move(onStateChange), std::move(onStart), std::move(onError))
    , ipcFactory_(std::move(ipcFactory))
    , audioClockManager_(std::move(audioClockManager))
    , userConfigProvider_(std::move(userConfigProvider))
    , sslVerification_(sslVerification)
    , customYandexMusicConfig_(customYandexMusicConfig)
    , multiroomState_(std::make_shared<MultiroomState>())
    , stereoPairState_(std::make_shared<StereoPairState>())
    , spectrumProvider_(std::make_shared<YandexIO::SpectrumProvider>(ipcFactory_))
{
    const auto& mediadConfig = device_->configuration()->getServiceConfig("mediad");

    if (!mediadConfig.isMember("playbackParams")) {
        throw std::runtime_error("Missing playbackParams section in config file");
    }

    if (multiroomProvider) {
        YIO_LOG_INFO("Connect to multiroom provider " << multiroomProvider.get() << "...");
        multiroomProvider->multiroomState().connect([this](const auto& state) {
            handleMultiroomState(state);
        }, lifetime_);
    }

    if (stereoPairProvider) {
        YIO_LOG_INFO("Connect to stereo pair provider...");
        stereoPairProvider->stereoPairState().connect([this](const auto& state) {
            handleStereoPairState(state);
        }, lifetime_);
    }

    playbackConfig_ = mediadConfig["playbackParams"];
    smoothVolumeIntervalMs_.audioFocus = tryGetInt(mediadConfig, "smoothVolumeIntervalMs_audioFocus", 70);
    smoothVolumeIntervalMs_.pauseResume = tryGetInt(mediadConfig, "smoothVolumeIntervalMs_pauseResume", 50);

    playerFactory_ = std::move(playerFactory);
    type_ = PlayerType::YANDEX_MUSIC;
    const auto yandexMusicParams = YandexMusic::Params{
        sslVerification_ ? YandexMusic::Params::Ssl::Yes : YandexMusic::Params::Ssl::No,
        YandexMusic::Params::AutoPing::Yes,
    };
    yandexMusic_ = std::make_shared<YandexMusic>(device_, yandexMusicParams, customYandexMusicConfig_,
                                                 [this] { onError_(Error::AUTH); });

    defaultMaxVolume_ = tryGetDouble(mediadConfig, "yandexMusicVolume", 100) / 100;

    useNormalization_ = tryGetBool(customYandexMusicConfig, "useNormalization", true);
    updateSouphttpsrcConfig(customYandexMusicConfig);
    updateFailedRequestsConfig(customYandexMusicConfig);
    updateAudioClock(customYandexMusicConfig);
    updateMaxVolume(customYandexMusicConfig);

    /* Create thread that maintain AudioPlayer */
    playThread_ = std::thread(&YandexMusicPlayer::playLoop, this);

    if (!ownsFocus) {
        freeAudioFocus();
    }
}

YandexMusicPlayer::YandexMusicPlayer(std::shared_ptr<YandexIO::IDevice> device, std::shared_ptr<ipc::IIpcFactory> ipcFactory, bool sslVerification, bool ownsFocus,
                                     std::shared_ptr<AudioPlayerFactory> playerFactory,
                                     const Json::Value& customYandexMusicConfig)
    : YandexMusicPlayer(
          std::move(device), std::move(ipcFactory), nullptr, sslVerification, ownsFocus, std::move(playerFactory), nullptr, nullptr, nullptr, customYandexMusicConfig,
          []() {},
          []() {},
          [](Error /* errorType*/) {})
{
}

YandexMusicPlayer::~YandexMusicPlayer() {
    YIO_LOG_DEBUG("destructor");

    lifetime_.die();
    std::unique_lock<std::mutex> lock(playerStateMutex_);
    stopped_ = true;
    wasStarted_ = false;
    isPlaying_ = false;

    lock.unlock();
    notifyPlayThread();
    if (playThread_.joinable()) {
        playThread_.join();
    }

    // gracefully stop current player
    if (currentPlayer_ != nullptr) {
        currentPlayer_->stop();
        currentPlayer_->removeListeners();
        currentPlayer_.reset();
    }
    yandexMusic_.reset();
}

void YandexMusicPlayer::notifyPlayThread() {
    {
        std::lock_guard lock(playThreadNotifyMutex_);
        notified_ = true;
    }
    playCondVar_.notify_one();
}

void YandexMusicPlayer::waitPlayThreadNotified() {
    std::unique_lock lock(playThreadNotifyMutex_);
    playCondVar_.wait(lock, [this]() {
        return notified_;
    });
    notified_ = false;
}

void YandexMusicPlayer::playLoop() {
    while (!stopped_) {
        waitPlayThreadNotified();
        // toDestroy is needed here to resolve circular dependency when destroying currentPlayer_.
        // currentPlayer_ has own thread that could be waiting on playerStateMutex_
        // If we join that thread while playerStateMutex_ is held, deadlock is imminent
        std::unique_ptr<AudioPlayer> toDestroy;

        std::string exceptionMessage;
        try {
            std::unique_lock<std::mutex> playerStateLock(playerStateMutex_);
            if (stopped_) {
                break;
            }
            YIO_LOG_DEBUG("Player intent: " << int(playerIntent_.load()) << " SwitchTrackMode: " << int(switchTrackOptions_.mode));

            if (!currentTrack_) {
                YIO_LOG_ERROR_EVENT("YandexMusicPlayer.InvalidTrack", "YandexMusicPlayer playLoop current track is null")
                continue;
            }

            // handling play(), prev() and next()
            if (switchTrackOptions_.mode != SwitchTrackMode::NONE) {
                toDestroy = std::move(currentPlayer_);
                currentPlayer_ = createPlayer(currentTrack_, oneTimeSeekMs_);
                oneTimeSeekMs_ = -1;

                // play() will set right volume when return audio focus will be returned
                // prev() or next() don't need smooth change tracks if music is already playing
                if (switchTrackOptions_.mode == SwitchTrackMode::PLAY || isPlaying_) {
                    setVolumeWithTicksInternal(hasAudioFocus_ ? 1.0 : NO_AUDIO_FOCUS_VOLUME);
                    playerIntent_ = PlayerIntent::NONE;
                } else {
                    // prev() or next() with no playing music need volume fade in
                    playerIntent_ = switchTrackOptions_.pause ? PlayerIntent::NONE : PlayerIntent::RESUME;
                }

                reportEvent("startAudio");
                if (!switchTrackOptions_.pause) {
                    currentPlayer_->playAsync();
                }

                isPlaying_ = !switchTrackOptions_.pause;
                forceSendStateOnPause_ = isPlaying_;
                switchTrackOptions_.mode = SwitchTrackMode::NONE;
                switchTrackOptions_.pause = false;
                playerStateLock.unlock();
                onStateChange();
            }
        } catch (const std::exception& ex) {
            exceptionMessage = ex.what();
        } catch (...) {
            exceptionMessage = "Unexpected exception";
        }
        if (!exceptionMessage.empty()) {
            YIO_LOG_ERROR_EVENT("YandexMusicPlayer.FailedStartTrack", "Fail start new track: " << exceptionMessage);
            Json::Value event;
            event["message"] = exceptionMessage;
            reportEvent("musicPlayerFailToStartTrack", event);
            continue;
        }
        if (toDestroy != nullptr) {
            toDestroy->removeListeners();
            toDestroy.reset();
        }

        // handling pause() and resume()
        {
            std::unique_lock<std::mutex> playerStateLock(playerStateMutex_);
            if (currentPlayer_ != nullptr) {
                playerStateLock.unlock();
                handlePlayerIntent();
            } else {
                YIO_LOG_WARN("Attempt to pause/resume on null music player")
            }
        }
    }
}

void YandexMusicPlayer::handlePlayerIntent() {
    // Need this as smooth play/pause are not immediate, so we check playerIntent_ every iteration
    std::unique_lock<std::mutex> lock(playerStateMutex_);
    while (playerIntent_ != PlayerIntent::NONE && currentPlayer_ != nullptr) {
        PlayerIntent playerIntent = playerIntent_;
        if (playerIntent == PlayerIntent::RESUME) {
            // music was not playing
            forceSendStateOnPause_ = true;
            if (currentVolume_ < std::numeric_limits<double>::epsilon()) {
                resumeLatencyPoint_ = device_->telemetry()->createLatencyPoint();
                currentPlayer_->playAsync();
                lock.unlock();
                onStateChange();
                lock.lock();
            }

            if (withSmooth_) {
                // until volume is maximum increase volume by one step, then check playerIntent_ again
                if (!smoothVolumeUpTick(smoothVolumeIntervalMs_.pauseResume)) {
                    continue;
                }
            } else {
                // if don't need fade in, just set maximum possible volume
                setVolumeWithTicksInternal(hasAudioFocus_ ? 1.0 : NO_AUDIO_FOCUS_VOLUME);
            }
            YIO_LOG_DEBUG("handled player intent: RESUME");
            playerIntent_ = PlayerIntent::NONE;
        } else if (playerIntent == PlayerIntent::PAUSE) {
            if (withSmooth_) {
                // until volume is minimum decrease volume by one step, then check playerIntent_ again
                if (!smoothVolumeDownTick(smoothVolumeIntervalMs_.pauseResume)) {
                    continue;
                }
            } else {
                // if don't need fade out, just set minimum possible volume
                setVolumeWithTicksInternal(0.0);
            }

            playerIntent_ = PlayerIntent::NONE;

            pauseLatencyPoint_ = device_->telemetry()->createLatencyPoint();
            bool pauseSuccess = currentPlayer_->pause();

            // do not trigger event if pause had been already set in the past. Every event updates pause timestamp
            // that matters in BASS communication
            // pauseSuccess: true if AudioPlayer actually paused the stream, false otherwise
            // forceSendStateOnPause_: true if last user intent was pause, false otherwise
            // If at least one is true, that means state changed
            if (pauseSuccess || forceSendStateOnPause_) {
                lock.unlock();
                onStateChange();
                lock.lock();
            }
            forceSendStateOnPause_ = false;
            YIO_LOG_DEBUG("handled player intent: PAUSE");
        }
    }
}

// Must be called under playerStateMutex_
bool YandexMusicPlayer::smoothVolumeUpTick(int tickIntervalMs) {
    // volume will be restored with audio focus
    if (!hasAudioFocus_) {
        setVolumeInternal(NO_AUDIO_FOCUS_VOLUME);
        smoothTicks_ = 0.3; // 0.3 ^ 2 ~= 0.1
        return true;
    }

    smoothTicks_ += 0.1;
    if (smoothTicks_ < 1.0) {
        setVolumeInternal(smoothTicks_ * smoothTicks_);
        std::this_thread::sleep_for(std::chrono::milliseconds(tickIntervalMs));
        return false;
    }
    smoothTicks_ = 1.0;
    setVolumeInternal(1.0);
    return true;
}

// Must be called under playerStateMutex_
bool YandexMusicPlayer::smoothVolumeDownTick(int tickIntervalMs) {
    smoothTicks_ -= 0.1;
    if (smoothTicks_ > 0.1) {
        setVolumeInternal(smoothTicks_ * smoothTicks_);
        std::this_thread::sleep_for(std::chrono::milliseconds(tickIntervalMs));
        return false;
    }
    smoothTicks_ = 0.0;
    setVolumeInternal(0.0);
    return true;
}

void YandexMusicPlayer::setCurrentProgress(int position, int duration) {
    maybeSendProgressToMetrica(position, duration);
    yandexMusic_->setCurrentProgress(position, duration);
    /* Send progress to listeners */
    onStateChange();
}

void YandexMusicPlayer::maybeSendProgressToMetrica(int position, int duration) {
    int lastPosition = getCurrentPosition();
    if (position < lastPosition || (float)position / PROGRESS_HEARTBEAT_PERIOD_SEC > ceil((float)lastPosition /
                                                                                          PROGRESS_HEARTBEAT_PERIOD_SEC)) {
        sendHeartbeatToMetrica(position, duration);
    }
}

void YandexMusicPlayer::sendHeartbeatToMetrica(int position, int duration) {
    Json::Value eventPayload;
    eventPayload["percentPlayed"] = ((float)position / (float)duration) * 100;
    eventPayload["durationSec"] = duration;
    if (multiroomState_) {
        if (multiroomState_->mode == MultiroomState::Mode::MASTER || multiroomState_->mode == MultiroomState::Mode::SLAVE) {
            eventPayload["multiroomMode"] = MultiroomState::modeName(multiroomState_->mode);
            eventPayload["multiroomSessionId"] = multiroomState_->multiroomSessionId();
            auto multiroomTrackId = multiroomState_->multiroomTrackId();
            if (!multiroomTrackId.empty()) {
                eventPayload["multiroomTrackId"] = multiroomTrackId;
            }
        }
    }
    if (stereoPairState_ && stereoPairState_->isStereoPair()) {
        eventPayload["stereoPairRole"] = StereoPairState::roleName(stereoPairState_->role);
        eventPayload["stereoPairPartnerDeviceId"] = stereoPairState_->partnerDeviceId;
        eventPayload["stereoPairPlayerStatus"] = StereoPairState::stereoPlayerStatusName(stereoPairState_->stereoPlayerStatus);
    }
    std::stringstream durationStream;
    durationStream << ((float)position / (float)duration) * 100;
    YIO_LOG_TRACE("sendHeartbeatToMetrica, " + durationStream.str() + "%");
    reportEvent("progressHeartbeatMusic", eventPayload);
}

unsigned int YandexMusicPlayer::getCurrentPosition() const {
    return yandexMusic_->getCurrentPosition();
}

std::shared_ptr<YandexMusic::Track> YandexMusicPlayer::getCurrentTrack() {
    std::lock_guard lock(playerStateMutex_);
    return currentTrack_;
}

std::unique_ptr<AudioPlayer> YandexMusicPlayer::createPlayer(const std::shared_ptr<YandexMusic::Track>& track, int initialSeekMs) {
    const auto& params = playerFactory_->createParams();
    auto config = customYandexMusicConfig_;
    jsonMerge(playbackConfig_, config);
    params->fromConfig(config);
    params->setURI(track->url);
    if (initialSeekMs > 0) {
        params->setInitialSeekMs(initialSeekMs);
    }
    params->setSoupHttpSrcConfig(soupHttpSrcConfig_);
    params->setChannel(audioPlayerChannelUnsafe());
    if (useNormalization_ && track->normalization.has_value()) {
        const auto& normalization = track->normalization;
        YIO_LOG_INFO("Apply normalization with values: tp " << normalization->truePeak
                                                            << ", loudness: " << normalization->integratedLoudness
                                                            << ", targetLufs: " << normalization->targetLufs);
        params->setNormalization(*track->normalization);
    }
    if (useNetClock_) {
        if (multiroomState_->peers.size() > 0 || stereoPairState_->isStereoPair()) {
            auto localAudioClock = audioClockManager_->localAudioClock();
            YIO_LOG_INFO("Use net clock for gstreamer pipeline: clock_id=" << std::string{localAudioClock->clockId()});
            params->setAudioClock(std::move(localAudioClock));
        } else {
            YIO_LOG_INFO("Use default clock for gstreamer pipeline because no any other multiroom devices around");
        }
    }
    std::unique_ptr<AudioPlayer> currentPlayer = playerFactory_->createPlayer(*params);
    currentPlayer->addListener(std::make_shared<SpectrumListener>(spectrumProvider_));
    currentPlayer->addListener(std::make_shared<MPListener>(device_, this));
    currentPlayer->setEqualizerConfig(equalizerConfig_);

    YIO_LOG_INFO("YandexMusicPlayer created: " << jsonToString(currentPlayer->debug(), true));
    return currentPlayer;
}

bool YandexMusicPlayer::isPlaying() const {
    std::lock_guard lock(playerStateMutex_);
    return isPlaying_;
}

void YandexMusicPlayer::play(const Json::Value& options) {
    YIO_LOG_INFO("play");
    const std::string uid = options["uid"].asString();
    const std::string sessionId = options["session_id"].asString();
    const std::string token = options["token"].asString();
    const std::string deviceId = options["device_id"].asString();
    const float offsetSec = options["offsetSec"].isNull() ? 0.0f : options["offsetSec"].asFloat();
    std::lock_guard lock(playerStateMutex_);
    checkLastPlayCommandInternal();

    setAuthData(uid, sessionId, token, deviceId);
    // vins_init_request_id should be set simultaneously with sessionId
    multiroomToken_ = options["multiroom_token"].asString();
    vinsInitRequestId_ = options["vins_init_request_id"].isNull() ? "" : options["vins_init_request_id"].asString();
    if (!vinsInitRequestId_.empty()) {
        lastPlayCommandTs_ = getNowTimestampMs();
    }
    currentVinsRequestId_ = vinsInitRequestId_;
    playPauseId_ = makeUUID();
    lastPlayCommandTimePoint_ = std::chrono::steady_clock::now();
    reportEvent("musicPlayerPlayRequest");

    switchTrack(SwitchTrackMode::PLAY, offsetSec);
}

void YandexMusicPlayer::pause(bool smooth, const std::string& vinsRequestId) {
    YIO_LOG_INFO("Music pause, smooth: " << smooth);
    std::lock_guard lock(playerStateMutex_);
    currentVinsRequestId_ = vinsRequestId;
    playPauseId_ = makeUUID();
    withSmooth_ = smooth;
    if (PlayerIntent::NONE == playerIntent_.exchange(PlayerIntent::PAUSE)) {
        // music was playing so we need to notify playing loop
        notifyPlayThread();
    } else {
        YIO_LOG_INFO("not notifying pause");
    }
    isPlaying_ = false;
}

void YandexMusicPlayer::discard() {
    std::unique_lock<std::mutex> lock(playerStateMutex_);
    playPauseId_ = makeUUID();
    const auto yandexMusicParams = YandexMusic::Params{
        sslVerification_ ? YandexMusic::Params::Ssl::Yes : YandexMusic::Params::Ssl::No,
        YandexMusic::Params::AutoPing::Yes,
    };
    yandexMusic_ = std::make_shared<YandexMusic>(device_, yandexMusicParams, customYandexMusicConfig_,
                                                 [this] { onError_(Error::AUTH); });
    wasStarted_ = false;
    isPlaying_ = false;
}

void YandexMusicPlayer::resume(bool smooth, const std::string& vinsRequestId) {
    YIO_LOG_INFO("Music resume, smooth: " << smooth);
    std::lock_guard lock(playerStateMutex_);
    currentTrack_ = yandexMusic_->start();
    currentVinsRequestId_ = vinsRequestId;
    playPauseId_ = makeUUID();
    withSmooth_ = smooth;
    if (PlayerIntent::NONE == playerIntent_.exchange(PlayerIntent::RESUME)) {
        // music was paused so we need to notify playing loop
        notifyPlayThread();
    } else {
        YIO_LOG_INFO("not notifying resume");
    }
    isPlaying_ = true;
}

void YandexMusicPlayer::seekTo(int seconds) {
    YIO_LOG_INFO("seek to: " << seconds << " sec");
    std::lock_guard lock(playerStateMutex_);
    if (currentPlayer_) {
        currentPlayer_->seek(seconds * 1000);

        // update position manually
        if (seconds >= 0 && seconds <= (int)yandexMusic_->getCurrentDuration()) {
            yandexMusic_->setCurrentPosition(seconds);
        }
    }
}

// Must be called under playerStateMutex_
void YandexMusicPlayer::setVolume(double volume) {
    setVolumeWithTicksInternal(volume);
}

// Must be called under playerStateMutex_
void YandexMusicPlayer::setVolumeWithTicksInternal(double volume) {
    smoothTicks_ = std::sqrt(volume);
    setVolumeInternal(volume);
}

// Must be called under playerStateMutex_
void YandexMusicPlayer::setVolumeInternal(double volume) {
    YIO_LOG_INFO("setVolume to: " << volume);
    currentVolume_ = volume;
    if (currentPlayer_) {
        currentPlayer_->setVolume(volume * maxVolume_);
    }
    YIO_LOG_INFO("successfully set volume to to: " << volume);
}

void YandexMusicPlayer::next(bool skip, bool setPause, bool ignoreIfNotPlaying) {
    std::lock_guard lock(playerStateMutex_);
    if (ignoreIfNotPlaying && !isPlaying_) {
        YIO_LOG_INFO("next ignored because playing state is false");
        return;
    }
    nextLatencyPoint_ = device_->telemetry()->createLatencyPoint();
    YIO_LOG_INFO("next with skip=" << skip << " setPause=" << setPause);
    switchTrackOptions_.pause = setPause;
    switchTrack(skip ? SwitchTrackMode::NEXT_WITH_SKIP : SwitchTrackMode::NEXT);
    device_->telemetry()->reportLatency(nextLatencyPoint_, "musicPlayerNextEnd");
}

void YandexMusicPlayer::prev(bool setPause) {
    YIO_LOG_INFO("prev with setPause=" << setPause);
    std::lock_guard lock(playerStateMutex_);
    switchTrackOptions_.pause = setPause;
    switchTrack(SwitchTrackMode::PREV);
}

// should be called under playerStateMutex_
void YandexMusicPlayer::switchTrack(SwitchTrackMode switchTrackMode, float offsetSec) {
    // at first need to pause the player
    if (currentPlayer_) {
        if (switchTrackMode == SwitchTrackMode::NEXT || switchTrackMode == SwitchTrackMode::NEXT_WITH_SKIP) {
            yandexMusic_->ignoreNextEnd();
        }
        currentPlayer_->pause();
    }

    // request track from backend
    if (switchTrackMode == SwitchTrackMode::PLAY) {
        try {
            if (wasStarted_) {
                currentTrack_ = yandexMusic_->restart();
            } else {
                currentTrack_ = yandexMusic_->start();
                wasStarted_ = true;
            }
        } catch (const RequestTimeoutException& e) {
            handleRequestTimeout("YandexMusicPlayTrackTimeout", e);
            throw;
        }

        // This constant should be large enough to never happen in real
        // scenarios and small enough to avoid integer cast overflow.
        // Hard upper limit is (INT_MAX / 1000) ~ 2 000 000
        const float maxOffsetSec = 86400.0f;
        if (offsetSec > 0.0f && offsetSec < maxOffsetSec) {
            YIO_LOG_INFO("playing track with offset: " << offsetSec << " sec");
            oneTimeSeekMs_ = (int)(offsetSec * 1000);
        } else {
            oneTimeSeekMs_ = -1;
        }
    } else {
        try {
            currentTrack_ = switchTrackMode == SwitchTrackMode::PREV
                                ? yandexMusic_->prev()
                                : yandexMusic_->next(switchTrackMode == SwitchTrackMode::NEXT_WITH_SKIP);
        } catch (const RequestTimeoutException& e) {
            handleRequestTimeout("YandexMusicSwitchTrackTimeout", e);
            throw;
        }
        oneTimeSeekMs_ = -1;
    }

    switchTrackOptions_.mode = switchTrackMode;
    notifyPlayThread();
}

void YandexMusicPlayer::like() {
    YIO_LOG_INFO("like");
    try {
        yandexMusic_->like();
    } catch (const RequestTimeoutException& e) {
        std::lock_guard lock(playerStateMutex_);
        handleRequestTimeout("YandexMusicLikeTimeout", e);
        throw;
    }
}

void YandexMusicPlayer::dislike() {
    YIO_LOG_INFO("dislike");
    try {
        yandexMusic_->dislike();
    } catch (const RequestTimeoutException& e) {
        std::lock_guard lock(playerStateMutex_);
        handleRequestTimeout("YandexMusicDislikeTimeout", e);
        throw;
    }
}

// must be called under playerStateMutex_
void YandexMusicPlayer::handleRequestTimeout(const std::string& tag, const RequestTimeoutException& error) {
    isPlaying_ = false;
    YIO_LOG_ERROR_EVENT("YandexMusicPlayer.RequestTimeout", tag << ": " << error.what());
    Json::Value event;
    event["what"] = error.what();
    reportEvent(tag, event);
}

void YandexMusicPlayer::handleMultiroomState(const std::shared_ptr<const MultiroomState>& newMultiroomState)
{
    std::lock_guard lock(playerStateMutex_);
    YIO_LOG_DEBUG("handleMultiroomState peers=" << newMultiroomState->peers.size()
                                                << ", mode=" << (int)newMultiroomState->mode
                                                << ", isPlaying_=" << isPlaying_
                                                << ", yandexMusic_=" << yandexMusic_.get()
                                                << ", currentChannel=" << (currentPlayer_ ? (int)currentPlayer_->channel() : -1)
                                                << ", seqnum=" << newMultiroomState->seqnum);
    if (multiroomState_->peers != newMultiroomState->peers) {
        if (newMultiroomState->peers.size() > 0) {
            std::string text;
            text.reserve(256);
            for (const auto& p : newMultiroomState->peers) {
                text += makeString(text.empty() ? "" : ", ", "{ deviceId=", p.deviceId, ", ipAddress=", p.ipAddress, "}");
            }
            YIO_LOG_INFO("Multiroom environment changed: mode=" << (int)newMultiroomState->mode << ", visible multiroom devices: " << text);
        } else {
            YIO_LOG_INFO("Multiroom environment changed: mode=" << (int)newMultiroomState->mode << ", no visible multiroom devices");
        }
    }

    if (newMultiroomState->mode == MultiroomState::Mode::MASTER &&
        isPlaying_ &&
        yandexMusic_ &&
        currentPlayer_) {
        if (newMultiroomState->peers.size() && !currentPlayer_->syncParams()) {
            YIO_LOG_DEBUG("Restart player for multiroom");
            switchTrack(SwitchTrackMode::PLAY, 0);
        }
    }

    multiroomState_ = newMultiroomState;
}

void YandexMusicPlayer::handleStereoPairState(const std::shared_ptr<const StereoPairState>& newStereoPairState)
{
    std::lock_guard lock(playerStateMutex_);
    YIO_LOG_DEBUG("handleStereoPairState "
                  "role="
                  << StereoPairState::roleName(newStereoPairState->role) << ", "
                                                                            "channel="
                  << StereoPairState::channelName(newStereoPairState->channel) << ", "
                                                                                  "visible peers count "
                  << multiroomState_->peers.size());

    auto oldAudioPlayerChannel = audioPlayerChannelUnsafe();
    stereoPairState_ = newStereoPairState;
    if (isPlaying_ &&
        yandexMusic_ &&
        currentPlayer_) {
        auto newAudioPlayerChannel = audioPlayerChannelUnsafe();
        if (oldAudioPlayerChannel != newAudioPlayerChannel) {
            YIO_LOG_DEBUG("Change sound channel for stereo pair from " << AudioPlayer::channelName(oldAudioPlayerChannel) << " to " << AudioPlayer::channelName(newAudioPlayerChannel));
            currentPlayer_->setChannel(newAudioPlayerChannel);
        }
    }
}

AudioPlayer::Channel YandexMusicPlayer::audioPlayerChannelUnsafe() const {
    if (multiroomState_->peers.empty()) {
        return AudioPlayer::Channel::ALL;
    }
    switch (stereoPairState_->channel) {
        case StereoPairState::Channel::RIGHT:
            return AudioPlayer::Channel::RIGHT;
        case StereoPairState::Channel::LEFT:
            return AudioPlayer::Channel::LEFT;
        default:
            return AudioPlayer::Channel::ALL;
    }
}

void YandexMusicPlayer::handleStop() {
    if (yandexMusic_) {
        yandexMusic_->stop();
    }
}

void YandexMusicPlayer::handleStart() {
    std::unique_lock<std::mutex> lock(playerStateMutex_);
    // drop failed counter, as some track started to play
    failedInRowPlayRequests_ = 0;
    if (onStart_) {
        const auto currentTrack = yandexMusic_->getCurrentTrack();
        if (currentTrack) {
            lock.unlock();
            onStart_();
            lock.lock();
        }
    }
}

NAlice::TDeviceState::TMusic YandexMusicPlayer::buildMusicState(YandexMusic::Track* currentTrack) const {
    Y_VERIFY(currentTrack != nullptr);
    NAlice::TDeviceState::TMusic musicState;

    musicState.MutablePlayer()->SetPause(!isPlaying_);
    if (!isPlaying_) {
        musicState.MutablePlayer()->SetTimestamp(getNowMs() / 1000);
    }

    musicState.MutableCurrentlyPlaying()->SetTrackId(TString(currentTrack->id));
    musicState.MutableCurrentlyPlaying()->SetLastPlayTimestamp(lastPlayCommandTs_);
    musicState.SetPlaylistOwner(TString(uid_));
    musicState.SetSessionId(TString(sessionId_));
    musicState.SetLastPlayTimestamp(lastPlayCommandTs_);

    Json::Value jsonTrackInfo = currentTrack->fullJsonInfo;
    jsonTrackInfo["progress_sec"] = yandexMusic_->getCurrentPosition();
    jsonTrackInfo["type"] = currentTrack->isShot ? "shot" : "track";
    jsonTrackInfo["artists"] = currentTrack->artists;
    jsonTrackInfo["title"] = currentTrack->title;
    jsonTrackInfo["durationMs"] = currentTrack->durationMs;
    jsonTrackInfo["coverUri"] = currentTrack->coverUri;
    jsonTrackInfo["trackType"] = currentTrack->type;
    jsonTrackInfo["trackAlbumGenre"] = currentTrack->albumGenre;
    if (!vinsInitRequestId_.empty()) {
        jsonTrackInfo["vins_request_id"] = vinsInitRequestId_; // Need for multiroom
    }

    if (!multiroomToken_.empty()) {
        jsonTrackInfo["multiroom_token"] = multiroomToken_; // Need for multiroom
    }

    const auto nextTrack = yandexMusic_->getNextTrack();
    jsonTrackInfo["nextTrackType"] = nextTrack ? nextTrack->type : "";
    jsonTrackInfo["nextTrackAlbumGenre"] = nextTrack ? nextTrack->albumGenre : "";
    jsonTrackInfo["nextTrackTitle"] = nextTrack ? nextTrack->title : "";

    if (currentPlayer_) {
        jsonTrackInfo["url"] = currentTrack->url;
        if (auto syncParams = currentPlayer_->syncParams()) {
            jsonTrackInfo["basetime_ns"] = static_cast<int64_t>(syncParams->basetime.count());
            jsonTrackInfo["position_ns"] = static_cast<int64_t>(syncParams->position.count());
        }

        if (auto normalization = currentPlayer_->params().normalization()) {
            jsonTrackInfo["normalization"]["true_peak"] = normalization->truePeak;
            jsonTrackInfo["normalization"]["integrated_loudness"] = normalization->integratedLoudness;
            jsonTrackInfo["normalization"]["target_lufs"] = normalization->targetLufs;
        }
    }

    const auto currentPlaylist = yandexMusic_->getCurrentPlaylist();
    jsonTrackInfo["playlist_id"] = currentPlaylist.id;
    jsonTrackInfo["playlist_type"] = currentPlaylist.type;
    jsonTrackInfo["playlist_description"] = currentPlaylist.description;
    jsonTrackInfo["playlist_shuffled"] = currentPlaylist.shuffled;
    jsonTrackInfo["playlist_repeat_mode"] = currentPlaylist.repeatMode;
    jsonTrackInfo["play_pause_id"] = playPauseId_;

    auto trackInfo = convertJsonToProtobuf<google::protobuf::Struct>(jsonToString(jsonTrackInfo));
    if (trackInfo.has_value()) {
        musicState.MutableCurrentlyPlaying()->MutableRawTrackInfo()->Swap(&trackInfo.value());
    }

    return musicState;
}

void YandexMusicPlayer::handleError() {
    // notify mediad with playback error callback
    if (onError_) {
        onError_(Error::PLAYBACK);
    }
}

// should be called under playerStateMutex_
void YandexMusicPlayer::setAuthData(const std::string& uid, const std::string& sessionId, const std::string& token,
                                    const std::string& deviceId) {
    YIO_LOG_INFO("session set to: " << sessionId << ", uid: " << uid);
    Json::Value eventJson;
    eventJson["session"] = sessionId;
    reportEvent("musicPlayerAuthSet", eventJson);
    yandexMusic_->setAuthData(uid, sessionId, token, deviceId);
    uid_ = uid;
    sessionId_ = sessionId;
}

std::optional<YandexMusicPlayer::TimePoint> YandexMusicPlayer::getAndResetLastPlayCommandTimePoint() {
    std::lock_guard lock(playerStateMutex_);
    const auto lastPlayCommandTimePoint = lastPlayCommandTimePoint_;
    lastPlayCommandTimePoint_.reset();
    return lastPlayCommandTimePoint;
}

void YandexMusicPlayer::reportLatencyPause() {
    device_->telemetry()->reportLatency(std::move(pauseLatencyPoint_), "musicPlayerPaused");
}

void YandexMusicPlayer::reportLatencyResume() {
    device_->telemetry()->reportLatency(std::move(resumeLatencyPoint_), "musicPlayerResumed");
}

void YandexMusicPlayer::reportLatencyNext() {
    device_->telemetry()->reportLatency(std::move(nextLatencyPoint_), "musicPlayerNextOnStart");
}

// onStateChange is actually complicated with respect to locks
// It should be inside of mutex to work, but onStateChange_ should not.
// We lock and unlock inside onStateChange
// avoiding ub inside onStateChange_ is callback's responsibility
void YandexMusicPlayer::onStateChange() {
    std::unique_lock<std::mutex> lock(playerStateMutex_);
    YIO_LOG_DEBUG("sendMusicState with isPaused: " << !isPlaying_);
    const auto currentTrack = yandexMusic_->getCurrentTrack();
    if (currentTrack && onStateChange_) {
        lock.unlock();
        onStateChange_();
    } else {
        YIO_LOG_INFO("No current Track or handler not set");
    }
}

void YandexMusicPlayer::seekForward(int seconds) {
    seekTo(getCurrentPosition() + seconds);
}

void YandexMusicPlayer::seekBackward(int seconds) {
    seekTo(getCurrentPosition() - seconds);
}

void YandexMusicPlayer::takeAudioFocus(bool smoothVolume) {
    std::lock_guard lock(playerStateMutex_);
    hasAudioFocus_ = true;
    if (isPlaying_) {
        if (!smoothVolume) {
            setVolume(1);
        } else {
            // stop raising volume if focus taken, stopped playing, or play()/pause()
            while (hasAudioFocus_ && isPlaying_ && playerIntent_ == PlayerIntent::NONE) {
                // rise volume tick by tick to maximum, then return
                if (smoothVolumeUpTick(smoothVolumeIntervalMs_.audioFocus)) {
                    return;
                }
            }
        }
    }
}

void YandexMusicPlayer::freeAudioFocus() {
    std::lock_guard lock(playerStateMutex_);
    hasAudioFocus_ = false;
    if (isPlaying_) {
        setVolume(NO_AUDIO_FOCUS_VOLUME);
    }
}

bool YandexMusicPlayer::hasAudioFocus() const {
    return hasAudioFocus_.load();
}

void YandexMusicPlayer::setEqualizerConfig(const YandexIO::EqualizerConfig& config) {
    std::lock_guard lock(playerStateMutex_);

    equalizerConfig_ = config;

    if (currentPlayer_) {
        YIO_LOG_INFO("YandexMusicPlayer::setEqualizerConfig");
        currentPlayer_->setEqualizerConfig(config);
    }
}

void YandexMusicPlayer::processCommand(const std::string& command, const Json::Value& options) {
    if (command == commands::LIKE) {
        like();
    } else if (command == commands::DISLIKE) {
        dislike();
    } else if (command == commands::NEXT) {
        const bool skip = tryGetBool(options, "skip", false);
        const bool setPause = tryGetBool(options, "setPause", false);
        const bool ignoreIfNotPlaying = tryGetBool(options, "ignoreIfNotPlaying", false);
        next(skip, setPause, ignoreIfNotPlaying);
    } else if (command == commands::PREVIOUS) {
        const bool setPause = tryGetBool(options, "setPause", false);
        prev(setPause);
    }
}

YandexMusicPlayer::ChangeConfigResult YandexMusicPlayer::updateConfig(const Json::Value& customYandexMusicConfig) {
    ChangeConfigResult changeConfigResult = ChangeConfigResult::NO_CHANGES;

    // normalization settings will be applied for next track
    useNormalization_ = tryGetBool(customYandexMusicConfig, "useNormalization", true);
    changeConfigResult = std::max(updateSouphttpsrcConfig(customYandexMusicConfig), changeConfigResult);
    changeConfigResult = std::max(updateFailedRequestsConfig(customYandexMusicConfig), changeConfigResult);
    changeConfigResult = std::max(updateAudioClock(customYandexMusicConfig), changeConfigResult);
    changeConfigResult = std::max(updateMaxVolume(customYandexMusicConfig), changeConfigResult);
    changeConfigResult = std::max(yandexMusic_->updateConfig(customYandexMusicConfig), changeConfigResult);
    customYandexMusicConfig_ = customYandexMusicConfig;
    return changeConfigResult;
}

// should be called under playerStateMutex_
std::string YandexMusicPlayer::getTrackIdInternal() const {
    if (currentTrack_) {
        return currentTrack_->id;
    }
    return "";
}

Player::State YandexMusicPlayer::getState()
{
    std::lock_guard lock(playerStateMutex_);

    NAlice::TDeviceState deviceState;
    if (const auto currentTrack = yandexMusic_->getCurrentTrack()) {
        auto musicState = buildMusicState(currentTrack.get());
        deviceState.MutableMusic()->Swap(&musicState);
    }

    return State{
        .deviceState = deviceState,
        .vinsRequestId = vinsInitRequestId_};
}

YandexMusicPlayer::ChangeConfigResult YandexMusicPlayer::updateSouphttpsrcConfig(const Json::Value& customYandexMusicConfig) {
    const Json::Value newConfig = tryGetJson(customYandexMusicConfig, "souphttpsrc", Json::Value::null);
    std::map<std::string, std::string> validated;
    if (!newConfig.empty()) {
        // validate with default values from https://gstreamer.freedesktop.org/documentation/soup/souphttpsrc.html?gi-language=c
        validated["retries"] = std::to_string(tryGetInt(newConfig, "retries", 3));
        validated["timeout"] = std::to_string(tryGetInt(newConfig, "timeout", 15));
    }
    if (soupHttpSrcConfig_ == validated) {
        return ChangeConfigResult::NO_CHANGES;
    }
    soupHttpSrcConfig_ = validated;
    return ChangeConfigResult::CHANGED;
}

YandexMusicPlayer::ChangeConfigResult YandexMusicPlayer::updateFailedRequestsConfig(const Json::Value& customYandexMusicConfig) {
    std::lock_guard lock(playerStateMutex_);
    YandexMusicPlayer::ChangeConfigResult result = ChangeConfigResult::NO_CHANGES;
    YIO_LOG_INFO("Suicide config received " << jsonToString(customYandexMusicConfig));
    int failedToNotify = tryGetInt(customYandexMusicConfig, "failedRequestsToNotify", 5);
    int failedToSuicide = tryGetInt(customYandexMusicConfig, "failedRequestsToSuicide", 0);
    bool isConfigChanged = failedToNotify != failedRequestsToNotify_ || failedToSuicide != failedRequestsToSuicide_;
    if (isConfigChanged) {
        failedRequestsToNotify_ = failedToNotify;
        failedRequestsToSuicide_ = failedToSuicide;
        result = ChangeConfigResult::CHANGED;
        YIO_LOG_INFO("Suicide config: reload failedRequestsToNotify = " << failedRequestsToNotify_
                                                                        << " and failedRequestsToSuicide = " << failedRequestsToSuicide_);
    }
    return result;
}

YandexMusicPlayer::ChangeConfigResult YandexMusicPlayer::updateAudioClock(const Json::Value& customYandexMusicConfig) {
    std::lock_guard lock(playerStateMutex_);
    YandexMusicPlayer::ChangeConfigResult result = ChangeConfigResult::NO_CHANGES;
    bool useNetClock = audioClockManager_ && tryGetBool(customYandexMusicConfig, "use_net_clock", true);
    if (useNetClock != useNetClock_) {
        result = ChangeConfigResult::NEED_RECREATE;
        useNetClock_ = useNetClock;
        YIO_LOG_DEBUG("Use " << (useNetClock_ ? "net clock" : "default clock") << " for gstreamer pipeline");
    }
    return result;
}

YandexMusicPlayer::ChangeConfigResult YandexMusicPlayer::updateMaxVolume(const Json::Value& customYandexMusicConfig) {
    std::lock_guard lock(playerStateMutex_);
    YandexMusicPlayer::ChangeConfigResult result = ChangeConfigResult::NO_CHANGES;
    double maxVolume = tryGetDouble(customYandexMusicConfig, "volume", defaultMaxVolume_ * 100) / 100;
    if (maxVolume != maxVolume_) {
        YIO_LOG_DEBUG("Max volume changed from " << maxVolume_ << " to " << maxVolume);
        maxVolume_ = maxVolume;
        result = ChangeConfigResult::CHANGED;
    }
    return result;
}

// should be called under playerStateMutex_
void YandexMusicPlayer::checkLastPlayCommandInternal() {
    // if track was started fine, then lastPlayCommandTimePoint_ should be already dropped
    if (lastPlayCommandTimePoint_.has_value()) {
        failedInRowPlayRequests_++;
        YIO_LOG_INFO("Suicide check failed in row: " << failedInRowPlayRequests_);
        if (failedInRowPlayRequests_ == failedRequestsToNotify_) {
            reportEvent("musicPlayerFailedInRow");
        } else if (failedRequestsToSuicide_ > 0 && failedInRowPlayRequests_ >= failedRequestsToSuicide_) {
            std::stringstream what;
            what << "Trying to suicide after failed play requests in a row: " << failedInRowPlayRequests_;
            throw SuicideException(what.str());
        }
    }
}
