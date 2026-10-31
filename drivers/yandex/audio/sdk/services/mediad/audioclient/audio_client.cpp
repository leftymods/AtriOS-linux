#include "audio_client.h"

#include "audio_client_utils.h"

#include <yandex_io/libs/audio_player/base/audio_player.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/protos/model_objects.pb.h>

#include <util/system/yassert.h>

#include <utility>
#include <span>

YIO_DEFINE_LOG_MODULE("media");

using namespace quasar;

namespace {

    // todo: ensure vsid generation is correct
    std::string generateVSID() {
        constexpr const char* STATION_VSID_ID = "STM";
        const std::string LETTERS_AND_DIGITS = "0123456789abcdefghijklmnopqrstuvwxyz";
        std::ostringstream builder;

        for (int i = 0; i < 44; ++i) {
            builder << LETTERS_AND_DIGITS[rand() % LETTERS_AND_DIGITS.size()];
        }

        builder << "x" << STATION_VSID_ID << "x";

        std::string version = "0000";
        builder << version << "x";
        builder << time(nullptr);

        return builder.str();
    }

    std::optional<AudioPlayer::Params::Normalization> extractNormalization(
        const proto::Audio& audio, const Json::Value& config) {
        const bool normalizationEnabled = tryGetBool(config, "useNormalization", true);
        if (!normalizationEnabled) {
            return std::nullopt;
        }
        using Normalization = AudioPlayer::Params::Normalization;
        const double targetLufs = tryGetDouble(config, "normalizationTargetLufs", Normalization::DEFAULT_TARGET_LUFS);

        // force normalization params from system_config for testing purposes while backend does not send values with directive
        if (config.isMember("normalization")) {
            const auto& n = config["normalization"];
            if (checkHasDoubleField(n, "truePeak") && checkHasDoubleField(n, "integratedLoudness")) {
                const Normalization normalization{
                    .truePeak = n["truePeak"].asDouble(),
                    .integratedLoudness = n["integratedLoudness"].asDouble(),
                    .targetLufs = targetLufs,
                };
                return normalization;
            }
        }

        // apply normalization values from directive
        const auto hasNormaliztionValues = [](const auto& audio) {
            if (!audio.has_normalization()) {
                return false;
            }
            const auto& normalization = audio.normalization();
            return normalization.has_true_peak() && normalization.has_integrated_loudness();
        };

        if (hasNormaliztionValues(audio)) {
            const auto& n = audio.normalization();
            const Normalization normalization{
                .truePeak = n.true_peak(),
                .integratedLoudness = n.integrated_loudness(),
                .targetLufs = targetLufs,
            };
            return normalization;
        }
        return std::nullopt;
    }

} // namespace

class AudioClient::PlayerEventListener: public AudioPlayer::SimpleListener {
public:
    PlayerEventListener(AudioClient& client)
        : client_(client)
    {
    }

public:
    void onError(const std::string& message) override {
        client_.handleError(message);
    }

    void onPaused() override {
        client_.handlePaused();
    }

    void onResumed() override {
        client_.handleResumed();
    }

    void onStart() override {
        client_.handleStart();
    }

    void onEnd() override {
        client_.handleEnd();
    }

    void onBufferingStart() override {
        client_.handleBufferingStart();
    }

    void onBufferingEnd() override {
        client_.handleBufferingEnd();
    }

    void onProgress(int position, int duration) override {
        client_.handleProgress(position, duration);
    }

    void onStopped() override {
        client_.handleStopped();
    }

    void onBufferStalled() override {
        client_.handleBufferStalled();
    }

private:
    AudioClient& client_;
};

AudioClient::AudioClient(
    std::shared_ptr<YandexIO::IDevice> device,
    YandexIO::DeviceContext& deviceContext,
    std::shared_ptr<const IAudioClockManager> audioClockManager,
    std::shared_ptr<AudioPlayerFactory> playerFactory,
    std::shared_ptr<AudioEventListener> eventListener,
    Json::Value playbackParams,
    Json::Value extraPlaybackParams,
    Json::Value customConfig,
    const proto::AudioPlayerDescriptor& descriptor,
    const proto::Audio& audio,
    std::shared_ptr<gogol::IGogolSession> gogol,
    std::shared_ptr<YandexIO::SpectrumProvider> spectrumProvider,
    bool hasAudioFocus)
    : playerDescriptor_(descriptor)
    , audio_(audio)
    , vsid_(generateVSID())
    , tag_(AudioClientUtils::buildLogTagWithVsid(descriptor, vsid_, nullptr))
    , hasAudioFocus_(hasAudioFocus)
    , shouldApplyAudioFocus_(audio.should_apply_audio_focus())
    , reportMetrics_(audio.report_metrics())
    , device_(std::move(device))
    , playerFactory_(std::move(playerFactory))
    , eventListener_(std::move(eventListener))
    , playbackParams_(std::move(playbackParams))
    , extraPlaybackParams_(std::move(extraPlaybackParams))
    , customConfig_(std::move(customConfig))
    , enablePlayedReporting_(tryGetBool(customConfig_, "enablePlayedReporting", true))
    , useNetClock_(tryGetBool(customConfig_, "useNetClock", false))
    , lastHeartbeatTime_(std::chrono::steady_clock::now())
    , gogol_(std::move(gogol))
    , deviceContext_(deviceContext)
    , spectrumListener_(std::make_shared<SpectrumListener>(std::move(spectrumProvider),
                                                           playerDescriptor_.type() == proto::AudioPlayerDescriptor::VOICE ? SpectrumListener::SpectrumType::VOICE : SpectrumListener::SpectrumType::MUSIC))
{
    Y_VERIFY(gogol_);
    // init gogol
    gogol_->setDeviceType(device_->platform());
    gogol_->setDeviceId(device_->deviceId());
    gogol_->setVersion(device_->softwareVersion());
    if (!audio_.url().empty()) {
        audio_.set_url(AudioClientUtils::setVSIDToUrlParams(audio_.url(), vsid_));
    }
    gogol_->setUrl(audio_.url());
    gogol_->setVsid(vsid_);

    gogol_->createPlayer();

    audio_.set_play_pause_id(makeUUID());
    audio_.set_position_sec(audio.initial_offset_ms() / 1000);
    audio_.set_played_sec(enablePlayedReporting_ ? 0 : audio.initial_offset_ms() / 1000);
    auto forcedTrackUrl = tryGetString(customConfig_, "forcedTrackUrl", "");
    if (!forcedTrackUrl.empty()) {
        YIO_LOG_INFO(tag_ << "AudioClient use forced track url");
        audio_.set_url(TString(forcedTrackUrl));
    }
    const auto params = getAudioParams(audio_, audioClockManager);
    if (params.normalization()) {
        const auto& n = *params.normalization();
        audio_.mutable_normalization()->set_true_peak(n.truePeak);
        audio_.mutable_normalization()->set_integrated_loudness(n.integratedLoudness);
        audio_.mutable_normalization()->set_target_lufs(n.targetLufs);
    } else {
        audio_.clear_normalization();
    }
    if (auto clock = params.audioClock()) {
        audio_.set_clock_id(std::string{clock->clockId()});
    } else {
        audio_.clear_clock_id();
    }
    YIO_LOG_DEBUG(tag_ << "using pipeline " << params.gstPipeline());
    audioPlayer_ = playerFactory_->createPlayer(params);
    Y_VERIFY(audioPlayer_ != nullptr);
    tag_ = AudioClientUtils::buildLogTagWithVsid(playerDescriptor_, vsid_, audioPlayer_.get());

    audioPlayer_->addListener(std::make_shared<PlayerEventListener>(*this));
    if (const auto spectrum = params.gstPipeline().find("spectrum"); spectrum != std::string::npos) {
        audioPlayer_->addListener(spectrumListener_);
    }
    YIO_LOG_INFO(tag_ << "AudioClient created: " << jsonToString(audioPlayer_->debug(), true));
}

AudioClient::~AudioClient() {
    gogol_->destroyPlayer("destruct");

    YIO_LOG_DEBUG(tag_ << "destruct");
    commandsQueue_.add([this]() {
        destroyPlayer();
    }, lifetime_);
    commandsQueue_.wait();
    lifetime_.die();
    commandsQueue_.destroy();
    YIO_LOG_DEBUG(tag_ << "destruct done");
}

void AudioClient::setVolume(double volume) {
    commandsQueue_.add([this, volume]() {
        currentVolume_ = volume;
        applyAudioFocus(hasAudioFocus_);
    }, lifetime_);
}

void AudioClient::setEqualizerConfig(const YandexIO::EqualizerConfig& config) {
    commandsQueue_.add([this, config]() {
        YIO_LOG_INFO("AudioClient::setEqualizerConfig");
        audioPlayer_->setEqualizerConfig(config);
    }, lifetime_);
}

void AudioClient::setMetadata(const proto::AudioMetadata& audioMetadata) {
    const auto& musicMeta = audioMetadata.music_metadata();
    commandsQueue_.add([this, musicMeta]() {
        YIO_LOG_INFO("AudioClient::setMetadata");

        auto music_meta = audio_.mutable_metadata()->mutable_music_metadata();

        if (musicMeta.has_id()) {
            music_meta->set_id(musicMeta.id());
        }
        if (musicMeta.has_type()) {
            music_meta->set_type(musicMeta.type());
        }
        if (musicMeta.has_shuffled()) {
            music_meta->set_shuffled(musicMeta.shuffled());
        }
        if (musicMeta.has_repeat_mode()) {
            music_meta->set_repeat_mode(musicMeta.repeat_mode());
        }
        if (musicMeta.has_next()) {
            auto next = music_meta->mutable_next();
            if (musicMeta.next().has_id()) {
                next->set_id(musicMeta.next().id());
            }
            if (musicMeta.next().has_type()) {
                next->set_type(musicMeta.next().type());
            }
        }
        if (musicMeta.has_prev()) {
            auto prev = music_meta->mutable_prev();
            if (musicMeta.prev().has_id()) {
                prev->set_id(musicMeta.prev().id());
            }
            if (musicMeta.prev().has_type()) {
                prev->set_type(musicMeta.prev().type());
            }
        }
    }, lifetime_);
}

void AudioClient::play(bool hasAudioFocus, bool setPause, AudioPlayer::Channel channel, Json::Value extraAnalytics) {
    commandsQueue_.add([this, hasAudioFocus, setPause, channel, extraAnalytics{std::move(extraAnalytics)}]() mutable {
        YIO_LOG_INFO(tag_ << "play"
                          << ", state=" << stateToString(currentState_)
                          << ", url=" << (audio_.has_url() ? audio_.url() : "")
                          << ", pause=" << setPause
                          << ", channel=" << AudioPlayer::channelName(channel)
                          << ", type=" << proto::AudioPlayerDescriptor::PlayerType_Name(playerDescriptor_.type())
                          << ", filePath=" << (audio_.has_file_path() ? audio_.file_path() : ""));

        // should not be able to recover from failed state, to not mess with states
        if (currentState_ == proto::AudioClientState::FAILED) {
            reportAnalyticsEvent("audioClientPlayOnFailed");
            return;
        }

        if (currentState_ == proto::AudioClientState::IDLE) {
            reportAnalyticsEvent("audioClientPlayRequest");
            createPlayLatencyPoint();
        }

        lastPlayTimestamp_ = getNowTimestampMs();
        setPause_ = setPause;
        channel_ = channel;
        extraAnalytics_ = std::move(extraAnalytics);

        // report idle state before playing
        currentState_ = proto::AudioClientState::IDLE;
        auto event = prepareEvent(proto::AudioClientEvent::STATE_CHANGED);
        eventListener_->onAudioEvent(event);

        audio_.set_play_pause_id(makeUUID());
        if (setPause) {
            firePauseEvents_ = false;
            audioPlayer_->pause();
        } else {
            audioPlayer_->setChannel(channel_);
            if (playerDescriptor_.is_multiroom_slave()) {
                if (!audio_.basetime_ns()) {
                    YIO_LOG_ERROR_EVENT("AudioClient.Error", tag_ << "Fail to play MULTIROOM with invalid params");
                } else {
                    auto basetime = std::chrono::nanoseconds{audio_.basetime_ns()};
                    auto position = std::chrono::nanoseconds{audio_.position_ns()};
                    YIO_LOG_DEBUG(tag_ << "AudioClient::play multiroom basetime=" << audio_.basetime_ns() << ", position=" << audio_.position_ns());
                    if (!audioPlayer_->playMultiroom(basetime, position)) {
                        YIO_LOG_WARN(tag_ << "Fail to start multiroom player");
                    }
                }
            } else {
                audioPlayer_->playAsync();
                YIO_LOG_DEBUG(tag_ << "AudioClient::play normal");
                updateMultiroomSync();
            }
            applyAudioFocus(hasAudioFocus);
        }
    }, lifetime_);
    commandsQueue_.wait();
}

void AudioClient::pushAudioData(const std::string& data) {
    commandsQueue_.add([this, data]() {
        if (appsrc_) {
            // split data by chunks to prevent interruption on different tts data. Splitting also give opportunity to take normal spectrum from gstreamer.
            std::span<const char> rawdata(data.data(), data.size());
            constexpr size_t CHUNK_SIZE = 8192;
            const size_t size = data.size();
            for (size_t i = 0; i < size; i += CHUNK_SIZE) {
                const auto subspan = rawdata.subspan(i, std::min(CHUNK_SIZE, size - i));
                appsrc_->pushData({subspan.begin(), subspan.end()});
            }
        }
    }, lifetime_);
}

void AudioClient::processEndOfStream() {
    commandsQueue_.add([this]() {
        YIO_LOG_INFO(tag_ << "processEndOfStream");
        if (appsrc_) {
            appsrc_->setDataEnd();
        }
    }, lifetime_);
}

void AudioClient::resume(bool hasAudioFocus) {
    commandsQueue_.add([this, hasAudioFocus]() {
        if (audioPlayer_) {
            YIO_LOG_INFO(tag_ << "resume");

            // should not be able to recover from failed state, to not mess with states
            if (currentState_ == proto::AudioClientState::FAILED) {
                reportAnalyticsEvent("audioClientResumeOnFailed");
                return;
            }

            audio_.set_play_pause_id(makeUUID());
            audioPlayer_->playAsync();
            updateMultiroomSync();
            applyAudioFocus(hasAudioFocus);
        }
    }, lifetime_);
}

void AudioClient::replay(bool hasAudioFocus) {
    commandsQueue_.add([this, hasAudioFocus]() {
        if (audioPlayer_) {
            YIO_LOG_INFO(tag_ << "replay");

            // should not be able to recover from failed state, to not mess with states
            if (currentState_ == proto::AudioClientState::FAILED) {
                reportAnalyticsEvent("audioClientReplayOnFailed");
                return;
            }

            if (currentState_ != proto::AudioClientState::FINISHED && currentState_ != proto::AudioClientState::STOPPED) {
                reportAnalyticsEvent("audioClientReplayOnNotFinished");
                return;
            }

            currentState_ = proto::AudioClientState::IDLE;
            auto event = prepareEvent(proto::AudioClientEvent::STATE_CHANGED);
            eventListener_->onAudioEvent(event);

            audioPlayer_->replayAsync();
            updateMultiroomSync();
            applyAudioFocus(hasAudioFocus);
        }
    }, lifetime_);
}

void AudioClient::pause(bool notifyDeviceContext) {
    commandsQueue_.add([this, notifyDeviceContext]() {
        if (audioPlayer_ && audioPlayer_->isPlaying()) {
            YIO_LOG_INFO(tag_ << "pause");

            lastStopTimestamp_ = getNowTimestampMs();
            firePauseEvents_ = true;
            audio_.set_play_pause_id(makeUUID());
            audioPlayer_->pause();
            if (notifyDeviceContext) {
                deviceContext_.fireMediaPaused(proto::MediaContentType::MUSIC);
            }
        }
    }, lifetime_);
}

void AudioClient::freeAudioFocus() {
    commandsQueue_.add([this]() {
        applyAudioFocus(false);
    }, lifetime_);
}

void AudioClient::takeAudioFocus() {
    commandsQueue_.add([this]() {
        applyAudioFocus(true);
    }, lifetime_);
}

void AudioClient::rewind(const proto::MediaRequest::Rewind& rewind) {
    commandsQueue_.add([this, rewind]() {
        YIO_LOG_INFO(tag_ << "rewind");

        // cannot rewind, if was not started at all
        if (lastPlayTimestamp_ == 0) {
            return;
        }

        int rewindSec = getRewindTarget(rewind);
        if (rewindSec < 0) {
            switchToEnd();
            return;
        }

        seekTo(rewindSec);
    }, lifetime_);
}

void AudioClient::startBuffering() {
    commandsQueue_.add([this]() {
        // cannot start buffering on already launched player
        if (lastPlayTimestamp_ != 0) {
            return;
        }
        YIO_LOG_INFO(tag_ << "startBuffering"
                          << ", url=" << (audio_.has_url() ? audio_.url() : ""));
        audioPlayer_->startBuffering();
    }, lifetime_);
}

void AudioClient::updatePlayerContext(const proto::MediaRequest& request) {
    commandsQueue_.add([this, request]() {
        fireResumeEvents_ = true;
        playerDescriptor_.set_player_id(request.player_descriptor().player_id());
        tag_ = AudioClientUtils::buildLogTagWithVsid(playerDescriptor_, vsid_, audioPlayer_.get());
        audio_.set_context(request.play_audio().context());
    }, lifetime_);
}

void AudioClient::seek(int seconds) {
    commandsQueue_.add([this, seconds]() {
        seekTo(seconds);
    }, lifetime_);
}

std::chrono::steady_clock::time_point AudioClient::getLastHeartbeatTime() const {
    return lastHeartbeatTime_.load();
}

void AudioClient::handleError(const std::string& message) {
    // todo: more smart parsing for errors
    gogol_->handleError("gstreamer.NotSpecifiedError", message);
    commandsQueue_.add([this, message] {
        // do not report errors, if was not started
        if (lastPlayTimestamp_ == 0) {
            return;
        }

        YIO_LOG_ERROR_EVENT("AudioClient.Error", tag_ << "onError " << message);
        Json::Value errorData = getAnalyticsJson();
        errorData["message"] = message;
        reportEvent("audioClientError", errorData);

        lastStopTimestamp_ = getNowTimestampMs();
        auto event = switchState(proto::AudioClientState::FAILED);
        if (event.has_state()) {
            event.set_error_text(TString(message));
            notifyListener(event);
        }
    }, lifetime_);
}

void AudioClient::handlePaused() {
    gogol_->handlePause();
    commandsQueue_.add([this] {
        YIO_LOG_INFO(tag_ << "handlePaused");

        auto event = switchState(proto::AudioClientState::PAUSED);
        if (event.has_state() && firePauseEvents_) {
            firePauseEvents_ = false;
            reportAnalyticsEvent("audioClientPaused");
            notifyListener(event);
        }
    }, lifetime_);
}

void AudioClient::handleResumed() {
    commandsQueue_.add([this] {
        YIO_LOG_INFO(tag_ << "handleResumed");

        auto event = switchState(proto::AudioClientState::PLAYING);
        if (event.has_state() && fireResumeEvents_) {
            reportAnalyticsEvent("audioClientResumed");
            notifyListener(event);
        }
    }, lifetime_);
}

void AudioClient::handleStart() {
    commandsQueue_.add([this] {
        YIO_LOG_INFO(tag_ << "handleStart");

        auto event = switchState(proto::AudioClientState::PLAYING);
        if (event.has_state()) {
            reportPlayLatency("audioClientPlayRequestFulfill", jsonToString(getAnalyticsJson()));
            notifyListener(event);
            forceSendHeartbeat_ = true;
        }
    }, lifetime_);
}

void AudioClient::handleEnd() {
    gogol_->handleEnd();
    commandsQueue_.add([this] {
        switchToEnd();
    }, lifetime_);
}

void AudioClient::handleBufferingStart() {
    commandsQueue_.add([this] {
        YIO_LOG_INFO(tag_ << "handleBufferingStart");
        fireResumeEvents_ = false;
    }, lifetime_);
}

void AudioClient::handleBufferingEnd() {
    gogol_->handleStalledEnd();
    commandsQueue_.add([this] {
        YIO_LOG_INFO(tag_ << "handleBufferingEnd");
        fireResumeEvents_ = true;
    }, lifetime_);
}

void AudioClient::handleProgress(int position, int duration) {
    gogol_->handleProgress(std::chrono::seconds(position), std::chrono::seconds(duration));
    commandsQueue_.add([this, position, duration] {
        YIO_LOG_DEBUG(tag_ << "onProgress duration: " << duration << " position: " << position);

        if (AudioClientUtils::isValidProgress(position, duration)) { // set position only in proper range
            const int played = position - audio_.position_sec() > 0 ? position - audio_.position_sec() : 0;
            audio_.set_played_sec(enablePlayedReporting_ ? audio_.played_sec() + played : position);
            audio_.set_position_sec(position);
            audio_.set_duration_sec(duration);
        }

        auto now = std::chrono::steady_clock::now();
        if (now - lastHeartbeatTime_.load() > heartbeatPeriod_ || forceSendHeartbeat_) {
            lastHeartbeatTime_ = now;
            if (playerDescriptor_.is_multiroom_slave()) {
                reportAnalyticsEvent("multiroomHeartbeat");
            } else {
                reportAnalyticsEvent("audioClientHeartbeat");
            }
            forceSendHeartbeat_ = false;
        }

        updateMultiroomSync();

        auto event = prepareEvent(proto::AudioClientEvent::HEARTBEAT);
        notifyListener(event);
    }, lifetime_);
}

void AudioClient::handleStopped() {
    commandsQueue_.add([this] {
        switchToStopped();
    }, lifetime_);
}

void AudioClient::handleBufferStalled() {
    gogol_->handleStalled();
    commandsQueue_.add([this] {
        reportAnalyticsEvent("audioClientBufferStalled");
    }, lifetime_);
}

/*
 * Methods to be called only inside commandsQueue_, so no additional synchronisation is required
 */
void AudioClient::destroyPlayer() {
    if (!audioPlayer_) {
        return;
    }

    YIO_LOG_INFO(tag_ << "destroying player...");
    if (appsrc_) {
        appsrc_->setDataEnd();
    }

    switchToStopped();
    audioPlayer_->removeListeners();
    audioPlayer_.reset();
    spectrumListener_->onPaused();
    YIO_LOG_INFO(tag_ << "player destroyed");
}

void AudioClient::switchToEnd() {
    YIO_LOG_INFO(tag_ << "switchToEnd");
    lastStopTimestamp_ = getNowTimestampMs();
    auto event = switchState(proto::AudioClientState::FINISHED);
    if (event.has_state()) {
        reportAnalyticsEvent("audioClientFinished");
        notifyListener(event);
    }
}

void AudioClient::switchToStopped() {
    YIO_LOG_INFO(tag_ << "switchToStopped");
    lastStopTimestamp_ = getNowTimestampMs();
    auto event = switchState(proto::AudioClientState::STOPPED);
    if (event.has_state()) {
        reportAnalyticsEvent("audioClientStopped");
        notifyListener(event);
    }
}

proto::AudioClientEvent AudioClient::switchState(const proto::AudioClientState& toState) {
    proto::AudioClientEvent eventToReport;

    YIO_LOG_DEBUG(tag_ << "switchState try change current state from " << proto::AudioClientState_Name(currentState_) << " to " << proto::AudioClientState_Name(toState));
    // do not change state if terminated or already in target state
    const bool terminatedState = (currentState_ == proto::AudioClientState::FINISHED || currentState_ == proto::AudioClientState::STOPPED || currentState_ == proto::AudioClientState::FAILED);
    if (currentState_ == toState || terminatedState) {
        return eventToReport;
    }

    currentState_ = toState;
    YIO_LOG_DEBUG(tag_ << "switchState current state changed to " << proto::AudioClientState_Name(toState));

    return prepareEvent(proto::AudioClientEvent::STATE_CHANGED);
}

Json::Value AudioClient::getAnalyticsJson() const {
    Json::Value info = extraAnalytics_;

    if (playerDescriptor_.has_type()) {
        info["type"] = playerDescriptor_.type();
    }

    if (audio_.has_id()) {
        info["id"] = audio_.id();
    }

    if (audio_.has_url()) {
        info["url"] = audio_.url();
    }

    if (audio_.has_analytics_context()) {
        info["vins_request_id"] = audio_.analytics_context().vins_request_id();
        info["name"] = audio_.analytics_context().name();
        const auto& parentId = audio_.analytics_context().vins_parent_request_id();
        if (!parentId.empty()) {
            info["vins_parent_request_id"] = parentId;
        }
    }

    info["position_sec"] = audio_.position_sec();
    info["duration_sec"] = audio_.duration_sec();
    if (audio_.duration_sec() > 0) {
        info["percent_played"] = ((float)audio_.position_sec() / (float)audio_.duration_sec()) * 100;
    }

    return info;
}

proto::AudioClientEvent AudioClient::prepareEvent(const proto::AudioClientEvent_Event eventType) const {
    auto event = proto::AudioClientEvent();
    event.set_event(eventType);
    event.set_state(currentState_);
    event.mutable_audio()->CopyFrom(audio_);
    event.mutable_player_descriptor()->CopyFrom(playerDescriptor_);
    event.set_last_play_timestamp(lastPlayTimestamp_);
    event.set_last_stop_timestamp(lastStopTimestamp_);
    return event;
}

int AudioClient::getRewindTarget(const proto::MediaRequest::Rewind& rewind) const {
    int rewindSec = -1;
    if (rewind.has_type() && rewind.has_amount()) {
        if (rewind.type() == proto::MediaRequest::ABSOLUTE) {
            rewindSec = (int)rewind.amount();
        } else if (rewind.type() == proto::MediaRequest::FORWARD) {
            rewindSec = audio_.position_sec() + (int)rewind.amount();
        } else if (rewind.type() == proto::MediaRequest::BACKWARD) {
            rewindSec = audio_.position_sec() - (int)rewind.amount();
        }
    }

    if (rewindSec < 0) {
        YIO_LOG_INFO(tag_ << "rewind to the beginning of stream");
        rewindSec = 0;
    }

    if (audio_.duration_sec() > 0 && rewindSec > audio_.duration_sec()) {
        YIO_LOG_INFO(tag_ << "rewind to the end of stream");
        rewindSec = -1;
    }

    return rewindSec;
}

void AudioClient::applyAudioFocus(bool hasAudioFocus) {
    if (audioPlayer_ != nullptr && shouldApplyAudioFocus_) {
        YIO_LOG_INFO(tag_ << "applyAudioFocus=" << hasAudioFocus);
        hasAudioFocus_ = hasAudioFocus;
        if (hasAudioFocus) {
            audioPlayer_->setVolume(currentVolume_);
        } else {
            audioPlayer_->setVolume(currentVolume_ * 0.1);
        }
    }
}

AudioPlayer::Params AudioClient::getAudioParams(const proto::Audio& audio, const std::shared_ptr<const IAudioClockManager>& audioClockManager) {
    const auto format = getFormatForConfig(audio.format());
    auto paramsJson = extraPlaybackParams_;
    jsonMerge(playbackParams_[format], paramsJson);
    auto params = playerFactory_->createParams()->fromConfig(paramsJson);

    if (audio.format() == proto::Audio::AUDIO_FILE) {
        params.setFilePath(audio.file_path());
    } else if (params.isStreamMode()) {
        appsrc_ = std::make_shared<StreamAppSrc>();
        if (audio.has_use_fake_volume_element()) {
            appsrc_->setUseVolumeElementStub(audio.use_fake_volume_element());
        }
        if (audio.has_input_stream_rate()) {
            appsrc_->setSampleRate(audio.input_stream_rate());
        }
        params.setStreamSrc(appsrc_);
    } else {
        params.setURI(audio.url());
        if (audio.has_initial_offset_ms()) {
            params.setInitialSeekMs(audio.initial_offset_ms());
        }
    }

    const auto normalization = extractNormalization(audio, customConfig_);
    if (normalization.has_value()) {
        YIO_LOG_INFO(tag_ << "Apply normalization values: truePeak: " << normalization->truePeak
                          << ", integratedLoudness: " << normalization->integratedLoudness
                          << ", targetLufs: " << normalization->targetLufs);
        params.setNormalization(*normalization);
    }
    // FIXME: Multiroom clock is not supported for HLS streams for now: SK-5911
    const bool isHLS = audio.format() == proto::Audio::HLS;
    if (isHLS || (playerDescriptor_.type() != proto::AudioPlayerDescriptor::AUDIO && !playerDescriptor_.is_multiroom_slave())) {
        YIO_LOG_INFO(tag_ << "Use default clock for non-audio gstreamer pipeline (player type is " << proto::AudioPlayerDescriptor::PlayerType_Name(playerDescriptor_.type()) << ")");
    } else if (audioClockManager) {
        const auto& clockId = audio.clock_id();
        auto localAudioClock = audioClockManager->localAudioClock();
        if (clockId.empty() || localAudioClock->clockId() == clockId) {
            YIO_LOG_INFO(tag_ << "Use local audio clock for gstreamer pipeline: clock_id=" << std::string{localAudioClock->clockId()});
            params.setAudioClock(std::move(localAudioClock));
        } else {
            auto removeAudioClock = audioClockManager->netAudioClock(clockId);
            if (!removeAudioClock) {
                throw std::runtime_error("Fail to get remove audio clock " + clockId);
            }
            YIO_LOG_INFO(tag_ << "Use remote audio clock for gstreamer pipeline: clock_id=" << std::string{removeAudioClock->clockId()});
            params.setAudioClock(std::move(removeAudioClock));
        }
        if (playerDescriptor_.is_multiroom_slave()) {
            params.setMode(AudioPlayer::Params::Mode::SLAVE);
        }
        if (params.initialOffsetMs() <= 0) {
            // A temporary solution, until we understand what caused the multiroom desynchronization. Ticket ALICE-13713
            params.setInitialSeekMs(1);
        }
    } else {
        YIO_LOG_INFO(tag_ << "Use default clock for gstreamer pipeline");
    }

    params.setChannel(channel_);

    return params;
}

void AudioClient::seekTo(int second) {
    YIO_LOG_INFO(tag_ << " seekTo " << second);

    if (audioPlayer_->seek(second * 1000)) {
        audio_.set_position_sec(second);
        if (!enablePlayedReporting_) {
            audio_.set_played_sec(second);
        }
        updateMultiroomSync();
    }
}

std::string AudioClient::stateToString(proto::AudioClientState value) {
    return proto::AudioClientState_Name(value);
}

void AudioClient::notifyListener(const proto::AudioClientEvent& event) const {
    // notify listener only if player was started or in silent mode
    if (lastPlayTimestamp_ == 0 || silent_.load()) {
        return;
    }

    eventListener_->onAudioEvent(event);
}

void AudioClient::setSilent(bool silent) {
    silent_ = silent;
}

void AudioClient::reportAnalyticsEvent(const std::string& event) {
    reportEvent(event, getAnalyticsJson());
}

void AudioClient::reportEvent(const std::string& event, Json::Value payload) {
    if (reportMetrics_) {
        if (audio_.has_analytics_context()) {
            payload["setraceRequestId"] = audio_.analytics_context().vins_request_id();
        }
        device_->telemetry()->reportEvent(event, jsonToString(payload));
    }
}

void AudioClient::reportPlayLatency(const std::string& eventName, const std::string& eventJson) {
    if (reportMetrics_) {
        device_->telemetry()->reportLatency(std::move(playLatencyPoint_), eventName, eventJson);
    }
}

void AudioClient::createPlayLatencyPoint() {
    if (reportMetrics_) {
        playLatencyPoint_ = device_->telemetry()->createLatencyPoint();
    }
}

void AudioClient::updateMultiroomSync()
{
    if (!audioPlayer_) {
        audio_.clear_basetime_ns();
        audio_.clear_position_ns();
    } else if (auto syncParams = audioPlayer_->syncParams()) {
        audio_.set_basetime_ns(syncParams->basetime.count());
        audio_.set_position_ns(syncParams->position.count());
    } else {
        audio_.clear_basetime_ns();
        audio_.clear_position_ns();
    }
}

std::string AudioClient::getFormatForConfig(proto::Audio::Format protoFormat) const {
    if (protoFormat == proto::Audio::HLS) {
        // we are using same pipeline for mp3 and hls
        protoFormat = proto::Audio::MP3;
    }
    std::string format = AudioClientUtils::audioFormatToString(protoFormat);
    if (!playbackParams_.isMember(format)) {
        YIO_LOG_WARN(tag_ << "Format " << format << " is not found in config. Fallback to MP3 format");
        format = "MP3";
    }
    return format;
}

std::optional<proto::AudioChannel> AudioClient::getAudioChannel() const {
    if (playerDescriptor_.has_audio_channel()) {
        return playerDescriptor_.audio_channel();
    }

    return std::nullopt;
}

proto::AudioPlayerDescriptor::PlayerType AudioClient::getDescriptorType() const {
    return playerDescriptor_.type();
}
