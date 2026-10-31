#include "audio_client_controller.h"

#include "audio_client_utils.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/protobuf_utils/json.h>
#include <yandex_io/libs/threading/periodic_executor.h>

#include <util/system/yassert.h>

YIO_DEFINE_LOG_MODULE("media");

using namespace quasar;

namespace {
    std::string getKeyForVolumeConfig(proto::AudioPlayerDescriptor::PlayerType type) {
        std::string key = proto::AudioPlayerDescriptor::PlayerType_Name(type);
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);
        return key;
    }

    const Json::Value& getAudioClientConfig(YandexIO::IDevice& device) {
        return device.configuration()->getServiceConfig("audioclient");
    }

    std::shared_ptr<gogol::GogolMetricsSender> makeGogolWorker(const std::shared_ptr<YandexIO::IDevice>& device) {
        const auto gogolCfg = getAudioClientConfig(*device)["gogol"];
        return std::make_shared<gogol::GogolMetricsSender>(device, tryGetInt(gogolCfg, "senderQueueSize", -1), tryGetBool(gogolCfg, "keepAlive", false));
    }

    proto::MediaRequest removeBinaryBlobForLog(const proto::MediaRequest& request) {
        if (!request.has_stream_data()) {
            return request;
        }

        // keep the fact that stream_data is here, but replace binary data with nice string
        auto copy = request;
        const TString streamData = "not_a_stream_data";
        copy.set_stream_data(streamData);
        return copy;
    }

    AudioPlayer::Channel convertChannel(StereoPairState::Channel channel) {
        switch (channel) {
            case StereoPairState::Channel::UNDEFINED:
                return AudioPlayer::Channel::ALL;
            case StereoPairState::Channel::ALL:
                return AudioPlayer::Channel::ALL;
            case StereoPairState::Channel::RIGHT:
                return AudioPlayer::Channel::RIGHT;
            case StereoPairState::Channel::LEFT:
                return AudioPlayer::Channel::LEFT;
        }
        return AudioPlayer::Channel::ALL;
    }

    std::string playersCountForLog(const std::map<std::string, std::shared_ptr<AudioClient>>& clients) {
        std::stringstream ss;
        std::map<proto::AudioPlayerDescriptor::PlayerType, int> typeToCount;
        for (const auto& [key, client] : clients) {
            ++typeToCount[client->getDescriptorType()];
        }
        ss << '[';
        for (const auto& [type, count] : typeToCount) {
            ss << proto::AudioPlayerDescriptor::PlayerType_Name(type) << ':' << count;
        }
        ss << ']';
        return ss.str();
    }

} // namespace

AudioClientController::AudioClientController(std::shared_ptr<YandexIO::IDevice> device,
                                             std::shared_ptr<ipc::IIpcFactory> ipcFactory,
                                             std::shared_ptr<const IAudioClockManager> audioClockManager,
                                             std::shared_ptr<IMultiroomProvider> multiroomProvider,
                                             std::shared_ptr<IStereoPairProvider> stereoPairProvider,
                                             std::shared_ptr<AudioPlayerFactory> playerFactory,
                                             std::shared_ptr<AudioEventListener> eventListener,
                                             const Json::Value& config)
    : device_(std::move(device))
    , ipcFactory_(std::move(ipcFactory))
    , playerFactory_(std::move(playerFactory))
    , audioClockManager_(std::move(audioClockManager))
    , multiroomProvider_(std::move(multiroomProvider))
    , stereoPairProvider_(std::move(stereoPairProvider))
    , eventListener_(std::move(eventListener))
    , defaultQueueMaxSize_(tryGetInt(config, "controllerQueueMaxSize", 100))
    , playbackParams_(config["playbackParams"])
    , cleanPlayersPeriodSeconds_(tryGetSeconds(config, "cleanPlayersPeriodSeconds", std::chrono::minutes(5)))
    , cleanPlayerTimeoutSeconds_(tryGetSeconds(config, "cleanPlayerTimeoutSeconds", std::chrono::minutes(30)))
    , suicideEnabled_(tryGetBool(config, "controllerQueueSuicideEnabled", false))
    , gogolWorker_(makeGogolWorker(device_))
    , commandsQueue_("AudioClientController", defaultQueueMaxSize_, [this](size_t size) { onQueueOverflow(size); })
    , deviceContext_(ipcFactory_, nullptr /* onConnected*/, false /* autoConnect*/)
    , spectrumProvider_(std::make_shared<YandexIO::SpectrumProvider>(ipcFactory_))
{
    scheduleCleanClients();
    const auto& audioClientConfig = getAudioClientConfig(*device_);
    const auto volume = tryGetJson(audioClientConfig, "volume", Json::Value());
    for (int iter = proto::AudioPlayerDescriptor::PlayerType_MIN; iter <= proto::AudioPlayerDescriptor::PlayerType_MAX; iter++) {
        auto type = static_cast<proto::AudioPlayerDescriptor::PlayerType>(iter);
        defaultVolume_[type] = tryGetDouble(volume, getKeyForVolumeConfig(type), 100) / 100;
    }
    deviceContext_.connectToSDK();
}

AudioClientController::~AudioClientController() {
    resetAllClients();
    commandsQueue_.destroy();
}

void AudioClientController::updateAudioClientConfig(const Json::Value& config) {
    const size_t maxSize = tryGetInt(config, "controllerQueueMaxSize", defaultQueueMaxSize_);
    const bool enabled = tryGetBool(config, "controllerQueueSuicideEnabled", suicideEnabled_.load());
    YIO_LOG_INFO("AudioClientController queue settings: " << maxSize << " suicideEnabled: " << enabled);
    commandsQueue_.setMaxSize(maxSize);
    suicideEnabled_.store(enabled);
    // queue may be overflowed now, so check it
    if (commandsQueue_.size() >= maxSize) {
        onQueueOverflow(maxSize);
    }

    commandsQueue_.add([this, config]() mutable {
        audioClientConfig_ = config;

        playersMaxSize_ = tryGetInt(audioClientConfig_, "playersMaxSize", 3);
        extraPlaybackParams_ = tryGetJson(audioClientConfig_, "extraPlaybackParams", Json::Value());
        YIO_LOG_INFO("AudioClientController::updateAudioClientConfig received " << jsonToString(audioClientConfig_));

        // update gogol settings
        handleGogolSettings(audioClientConfig_);

        const auto volume = tryGetJson(audioClientConfig_, "volume", Json::Value());
        for (int iter = proto::AudioPlayerDescriptor::PlayerType_MIN; iter <= proto::AudioPlayerDescriptor::PlayerType_MAX; iter++) {
            auto type = static_cast<proto::AudioPlayerDescriptor::PlayerType>(iter);
            double defaultVolume = defaultVolume_.count(type) ? defaultVolume_.at(type) : 1;
            configuredVolume_[type] = tryGetDouble(volume, getKeyForVolumeConfig(type), defaultVolume * 100) / 100;
        }
        for (const auto& [_, client] : activeClients_) {
            updateClientVolume(client);
        }
    });
}

void AudioClientController::setEqualizerConfig(const YandexIO::EqualizerConfig& config) {
    YIO_LOG_DEBUG(config);

    equalizerConfig_ = config;

    for (const auto& [_, client] : activeClients_) {
        client->setEqualizerConfig(equalizerConfig_);
    }
}

void AudioClientController::handleMediaRequest(const proto::MediaRequest& request) {
    commandsQueue_.add([this, request] {
        YIO_LOG_DEBUG("AudioClientController media request: " << convertMessageToDeepJsonString(removeBinaryBlobForLog(request)));

        if (request.has_clean_players()) {
            handleCleanClients(request.player_descriptor());
            return;
        }

        if (request.has_current_focused_channel()) {
            handleAudioFocus(request.current_focused_channel());
            return;
        }

        if (request.has_play_audio()) {
            handlePlayAudio(request);
            reportClientSize();
            return;
        }

        if (request.has_equalizer_config()) {
            setEqualizerConfig(
                YandexIO::EqualizerConfig::fromProto(request.equalizer_config()));
            return;
        }

        handleRequestToActiveClients(request);
    });
}

void AudioClientController::handleAudioFocus(proto::AudioChannel focusedChannel)
{
    if (currentFocusedChannel_ == focusedChannel) {
        return;
    }

    currentFocusedChannel_ = focusedChannel;

    for (const auto& [_, client] : activeClients_) {
        const auto optAudioChannel = client->getAudioChannel();
        if (optAudioChannel.has_value()) {
            if (optAudioChannel.value() == currentFocusedChannel_) {
                client->takeAudioFocus();
            } else {
                client->freeAudioFocus();
            }
        }
    }
}

void AudioClientController::handleEqualizerRequest(const proto::EqualizerConfig& config) {
    commandsQueue_.add([this, config] {
        setEqualizerConfig(
            YandexIO::EqualizerConfig::fromProto(config));
    });
}

void AudioClientController::resetAllClients() {
    commandsQueue_.add([this] {
        activeClients_.clear();
        cachedClients_.clear();
        YIO_LOG_INFO("All AudioClients removed");
    });
}

void AudioClientController::scheduleCleanClients() {
    commandsQueue_.addDelayed([this] {
        cleanOutdatedClients();
        scheduleCleanClients();
    }, cleanPlayersPeriodSeconds_);
}

void AudioClientController::cleanOutdatedClients() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = cachedClients_.cbegin(); it != cachedClients_.cend();) {
        if (now - it->second->getLastHeartbeatTime() > cleanPlayerTimeoutSeconds_) {
            YIO_LOG_INFO("cleanOutdatedClients cache " << it->first);
            it->second->setSilent(true);
            cachedClients_.erase(it++);
        } else {
            ++it;
        }
    }
    for (auto it = activeClients_.cbegin(); it != activeClients_.cend();) {
        const auto client = it->second;
        if (now - client->getLastHeartbeatTime() > cleanPlayerTimeoutSeconds_) {
            YIO_LOG_INFO("cleanOutdatedClients active, key " << it->first << ", type " << proto::AudioPlayerDescriptor::PlayerType_Name(client->getDescriptorType()));
            client->setSilent(true);
            activeClients_.erase(it++);
        } else {
            ++it;
        }
    }
}

void AudioClientController::reportClientSize() const {
    const int activeSize = activeClients_.size();
    const int cachedSize = cachedClients_.size();

    YIO_LOG_INFO("AudioClients active: " << playersCountForLog(activeClients_) << " cached: " << playersCountForLog(cachedClients_));
    Json::Value data;
    data["size"] = activeSize + cachedSize;
    device_->telemetry()->reportEvent("audioClientControllerPlayers", jsonToString(data));
}

void AudioClientController::onQueueOverflow(size_t size) {
    const auto enabled = suicideEnabled_.load();

    YIO_LOG_ERROR_EVENT("AudioClientController.QueueOverflow", "Overflow of callback queue " << size << ". suicide enabled: " << enabled);
    Json::Value event;
    event["size"] = (int)size;
    event["suicideEnabled"] = enabled;
    device_->telemetry()->reportEvent("AudioClientController.onOverflow", jsonToString(event));

    if (enabled) {
        // give metrics a chance to be sent
        std::this_thread::sleep_for(std::chrono::seconds(5));
        abort();
    }
}

void AudioClientController::handlePlayAudio(const proto::MediaRequest& request) {
    if (!request.has_play_audio()) {
        return;
    }

    if (request.is_prefetch()) {
        handlePlayAudioPrefetch(request);
        return;
    }

    const auto& playAudio = request.play_audio();
    const auto& descriptor = request.player_descriptor();
    const auto type = descriptor.type();
    const bool isAudio = (type == proto::AudioPlayerDescriptor::AUDIO);

    // if channel is not specified for the player, it will always has audio focus
    const bool hasAudioFocus = !descriptor.has_audio_channel() || descriptor.audio_channel() == currentFocusedChannel_;

    std::shared_ptr<AudioClient> activeClient;
    const auto& key = AudioClientUtils::getPlayerKey(descriptor);
    if (isAudio && cachedClients_.count(key)) {
        YIO_LOG_INFO("Reusing cached client of type " << proto::AudioPlayerDescriptor::PlayerType_Name(type) << " and key " << key);
        activeClient = std::move(cachedClients_.at(key));
        cachedClients_.erase(key);

        activeClient->updatePlayerContext(request);
    } else {
        YIO_LOG_INFO("Creating new client of type " << proto::AudioPlayerDescriptor::PlayerType_Name(type) << " and key: " << key);
        activeClient = std::make_shared<AudioClient>(device_, deviceContext_, audioClockManager_,
                                                     playerFactory_, eventListener_, playbackParams_, extraPlaybackParams_, audioClientConfig_,
                                                     descriptor, playAudio, makeGogolSession(type), spectrumProvider_, hasAudioFocus);
    }

    updateClientVolume(activeClient);
    activeClient->setEqualizerConfig(equalizerConfig_);

    const auto stereoPairState = (stereoPairProvider_ ? stereoPairProvider_->stereoPairState().value() : nullptr);
    const auto paused = playAudio.has_set_pause() && playAudio.set_pause();
    const auto channel = (isAudio && stereoPairState ? convertChannel(stereoPairState->channel) : AudioPlayer::Channel::ALL);
    Json::Value extraAnalytics;
    if (isAudio) {
        const auto multiroomState = (multiroomProvider_ ? multiroomProvider_->multiroomState().value() : nullptr);
        if (stereoPairState && stereoPairState->isStereoPair()) {
            extraAnalytics["stereoPairRole"] = StereoPairState::roleName(stereoPairState->role);
            extraAnalytics["stereoPairPartnerDeviceId"] = stereoPairState->partnerDeviceId;
            extraAnalytics["stereoPairPlayerStatus"] = StereoPairState::stereoPlayerStatusName(stereoPairState->stereoPlayerStatus);
        }
        if (multiroomState && multiroomState->broadcast) {
            extraAnalytics["multiroomMode"] = MultiroomState::modeName(multiroomState->mode);
            extraAnalytics["multiroomSessionId"] = multiroomState->multiroomSessionId();
            extraAnalytics["multiroomTrackId"] = multiroomState->multiroomTrackId();
        }
    }
    activeClient->play(hasAudioFocus, paused, channel, std::move(extraAnalytics));
    if (playAudio.has_analytics_context()) {
        std::unordered_map<std::string, std::string> setraceArgs = {
            {"setraceRequestId", playAudio.analytics_context().vins_request_id()}};
        device_->telemetry()->reportKeyValues("audioClientControllerPlayAudio", setraceArgs);
    }
    activeClients_[key] = activeClient;
}

void AudioClientController::updateClientVolume(const std::shared_ptr<AudioClient>& client) const {
    const auto type = client->getDescriptorType();
    double volume = 1.0;
    if (configuredVolume_.count(type)) {
        volume = configuredVolume_.at(type);
    } else if (defaultVolume_.count(type)) {
        volume = defaultVolume_.at(type);
    }
    YIO_LOG_DEBUG("Volume for player \"" << getKeyForVolumeConfig(type) << "\" is set to " << volume);
    client->setVolume(volume);
}

void AudioClientController::handlePlayAudioPrefetch(const proto::MediaRequest& request) {
    Y_VERIFY(request.has_play_audio());
    Y_VERIFY(request.is_prefetch());

    const auto type = request.player_descriptor().type();
    if (type != proto::AudioPlayerDescriptor::AUDIO) {
        YIO_LOG_ERROR_EVENT("AudioClientController.handlePlayAudioPrefetch",
                            "Prefetch received for player type " << proto::AudioPlayerDescriptor::PlayerType_Name(type));
        return;
    }

    const auto& key = AudioClientUtils::getPlayerKey(request.player_descriptor());
    if (cachedClients_.count(key)) {
        YIO_LOG_INFO("Clearing already cached client for key " << key);
        cachedClients_.erase(key);
    }

    if ((int)cachedClients_.size() >= playersMaxSize_) {
        YIO_LOG_INFO("Exceeding max size of cached players, so clear it");
        cachedClients_.clear();
    }

    YIO_LOG_INFO("Creating cached client for key " << key);
    auto cachedClient = std::make_shared<AudioClient>(device_, deviceContext_, audioClockManager_, playerFactory_,
                                                      eventListener_, playbackParams_, extraPlaybackParams_, audioClientConfig_,
                                                      request.player_descriptor(), request.play_audio(), makeGogolSession(type), spectrumProvider_, true);
    cachedClient->startBuffering();
    cachedClients_[key] = cachedClient;
}

void AudioClientController::handleRequestToActiveClients(const proto::MediaRequest& request)
{
    const auto& key = AudioClientUtils::getPlayerKey(request.player_descriptor());
    const auto type = request.player_descriptor().type();

    if (!key.empty()) {
        auto iter = activeClients_.find(key);
        if (iter == activeClients_.end()) {
            return;
        }

        YIO_LOG_INFO("handleRequestToClient key " << key << ", type " << proto::AudioPlayerDescriptor::PlayerType_Name(type));
        handleRequestToClient(request, iter->second);
    } else {
        for (const auto& [_, client] : activeClients_) {
            if (client->getDescriptorType() == type) {
                YIO_LOG_INFO("handleRequestToClient key " << key << ", type " << proto::AudioPlayerDescriptor::PlayerType_Name(type));
                handleRequestToClient(request, client);
            }
        }
    }
}

void AudioClientController::handleRequestToClient(const proto::MediaRequest& request, const std::shared_ptr<AudioClient>& client) {
    if (request.has_pause()) {
        const bool fireDeviceContext = request.has_fire_device_context() && request.fire_device_context();
        client->pause(fireDeviceContext);
    } else if (request.has_rewind()) {
        client->rewind(request.rewind());
    } else if (request.has_resume()) {
        client->resume(shouldClientHaveAudioFocus(client));
    } else if (request.has_stream_data()) {
        client->pushAudioData(request.stream_data());
    } else if (request.has_stream_data_end()) {
        client->processEndOfStream();
    } else if (request.has_metadata()) {
        client->setMetadata(request.metadata());
    } else if (request.has_replay()) {
        client->replay(shouldClientHaveAudioFocus(client));
    }
}

bool AudioClientController::shouldClientHaveAudioFocus(const std::shared_ptr<AudioClient>& client) const {
    const auto& optAudioChannel = client->getAudioChannel();
    return !optAudioChannel.has_value() || optAudioChannel.value() == currentFocusedChannel_;
}

void AudioClientController::handleCleanClients(const proto::AudioPlayerDescriptor& descriptor) {
    if (!descriptor.has_type()) {
        YIO_LOG_INFO("handleCleanClients: clean all players");
        activeClients_.clear();
        cachedClients_.clear();
        return;
    }
    const auto& key = AudioClientUtils::getPlayerKey(descriptor);
    const auto& type = descriptor.type();
    if (key.empty()) {
        // if key is not specified, erase all players of corresponding type
        YIO_LOG_INFO("Erase all players of type: " << proto::AudioPlayerDescriptor::PlayerType_Name(type));
        std::erase_if(activeClients_, [type](const auto& item) {
            const auto& [key, player] = item;
            return player->getDescriptorType() == type;
        });
        std::erase_if(cachedClients_, [type](const auto& item) {
            const auto& [key, player] = item;
            return player->getDescriptorType() == type;
        });
        return;
    }
    if (activeClients_.erase(key) != 0) {
        YIO_LOG_INFO("activeClients_.erase key " << key << ", type " << proto::AudioPlayerDescriptor::PlayerType_Name(type));
    }
    if (cachedClients_.erase(key) != 0) {
        YIO_LOG_INFO("cachedClients_.erase key " << key << ", type " << proto::AudioPlayerDescriptor::PlayerType_Name(type));
    }
}

void AudioClientController::handleGogolSettings(const Json::Value& config) {
    const auto& gogol = config["gogol"];
    const auto size = tryGetInt(gogol, "senderQueueSize", -1);
    if (size > 0) {
        gogolWorker_->setQueueSize(size);
    } else {
        gogolWorker_->setQueueSize(std::nullopt); // reset to default
    }
    gogolWorker_->setKeepAlive(tryGetBool(gogol, "keepAlive", false));
}

std::shared_ptr<quasar::gogol::IGogolSession> AudioClientController::makeGogolSession(proto::AudioPlayerDescriptor::PlayerType type) const {
    const auto enabled = tryGetBool(audioClientConfig_["gogol"], "enabled", true);
    const auto isAudio = type == proto::AudioPlayerDescriptor::AUDIO;
    // send gogol metrics for audio streams only
    if (enabled && isAudio) {
        return std::make_shared<gogol::GogolSession>(gogolWorker_);
    }
    return std::make_shared<gogol::NullGogolSession>();
}
