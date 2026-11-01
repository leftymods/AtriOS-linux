#pragma once

#include "audio_client.h"
#include "audio_event_listener.h"

#include <yandex_io/services/mediad/audio_clock_manager/i_audio_clock_manager.h>
#include <yandex_io/services/mediad/spectrum_listener/spectrum_provider.h>

#include <yandex_io/interfaces/multiroom/i_multiroom_provider.h>
#include <yandex_io/interfaces/stereo_pair/i_stereo_pair_provider.h>
#include <yandex_io/libs/audio_player/base/audio_player.h>
#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/equalizer_config/equalizer_config.h>
#include <yandex_io/libs/gogol/gogol_metrics_sender.h>
#include <yandex_io/libs/gogol/gogol_session.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/protos/quasar_proto.pb.h>
#include <yandex_io/sdk/private/device_context.h>

#include <map>
#include <string>
#include <utility>

namespace quasar {

    class AudioClientController {
    public:
        AudioClientController(std::shared_ptr<YandexIO::IDevice> device,
                              std::shared_ptr<ipc::IIpcFactory> ipcFactory,
                              std::shared_ptr<const IAudioClockManager> audioClockManager,
                              std::shared_ptr<IMultiroomProvider> multiroomProvider,
                              std::shared_ptr<IStereoPairProvider> stereoPairProvider,
                              std::shared_ptr<AudioPlayerFactory> playerFactory,
                              std::shared_ptr<AudioEventListener> eventListener,
                              const Json::Value& config);
        ~AudioClientController();

        void updateAudioClientConfig(const Json::Value& config);
        void handleMediaRequest(const proto::MediaRequest& request);
        void handleEqualizerRequest(const proto::EqualizerConfig& config);
        void resetAllClients();

    private:
        void scheduleCleanClients();
        void cleanOutdatedClients();

        // should be called only inside callback queue
        void handleAudioFocus(proto::AudioChannel focusedChannel);
        void handleCleanClients(const proto::AudioPlayerDescriptor& descriptor);
        void handlePlayAudio(const proto::MediaRequest& request);
        void handlePlayAudioPrefetch(const proto::MediaRequest& request);
        void handleRequestToActiveClients(const proto::MediaRequest& request);
        void handleRequestToClient(const proto::MediaRequest& request, const std::shared_ptr<AudioClient>& client);
        void reportClientSize() const;
        void updateClientVolume(const std::shared_ptr<AudioClient>& client) const;

        void onQueueOverflow(size_t size);

        void setEqualizerConfig(const YandexIO::EqualizerConfig& config);

        void handleGogolSettings(const Json::Value& config);
        std::shared_ptr<quasar::gogol::IGogolSession> makeGogolSession(proto::AudioPlayerDescriptor::PlayerType type) const;

        bool shouldClientHaveAudioFocus(const std::shared_ptr<AudioClient>& client) const;

    private:
        const std::shared_ptr<YandexIO::IDevice> device_;
        const std::shared_ptr<ipc::IIpcFactory> ipcFactory_;
        const std::shared_ptr<AudioPlayerFactory> playerFactory_;
        const std::shared_ptr<const IAudioClockManager> audioClockManager_;
        const std::shared_ptr<IMultiroomProvider> multiroomProvider_;
        const std::shared_ptr<IStereoPairProvider> stereoPairProvider_;
        const std::shared_ptr<AudioEventListener> eventListener_;

        using PlayerKey = std::string;
        std::map<PlayerKey, std::shared_ptr<AudioClient>> activeClients_;
        std::map<PlayerKey, std::shared_ptr<AudioClient>> cachedClients_;

        const size_t defaultQueueMaxSize_;

        Json::Value playbackParams_;
        Json::Value extraPlaybackParams_;

        // Volume settings from quasar.cfg
        std::map<proto::AudioPlayerDescriptor::PlayerType, double> defaultVolume_;

        // By default same as defaultVolume_, but some keys are overriden from backend
        std::map<proto::AudioPlayerDescriptor::PlayerType, double> configuredVolume_;

        // run task for players cleaning every cleanPlayersPeriodSeconds_
        const std::chrono::seconds cleanPlayersPeriodSeconds_;
        // clean player if it didn't heartbeat for more than cleanPlayerTimeoutSeconds_
        const std::chrono::seconds cleanPlayerTimeoutSeconds_;

        Json::Value audioClientConfig_;
        proto::AudioChannel currentFocusedChannel_{proto::AudioChannel::CONTENT_CHANNEL};
        int playersMaxSize_{3};
        std::atomic<bool> suicideEnabled_{false};

        std::shared_ptr<gogol::GogolMetricsSender> gogolWorker_;

        NamedCallbackQueue commandsQueue_;
        YandexIO::DeviceContext deviceContext_;

        YandexIO::EqualizerConfig equalizerConfig_;

        const std::shared_ptr<YandexIO::SpectrumProvider> spectrumProvider_;
    };
} // namespace quasar
