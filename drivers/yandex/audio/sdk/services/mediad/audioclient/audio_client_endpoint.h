#pragma once

#include <yandex_io/services/mediad/audio_clock_manager/i_audio_clock_manager.h>

#include <yandex_io/services/mediad/audioclient/audio_client_controller.h>
#include <yandex_io/services/mediad/audioclient/audio_event_listener.h>

#include <yandex_io/interfaces/multiroom/i_multiroom_provider.h>
#include <yandex_io/interfaces/stereo_pair/i_stereo_pair_provider.h>
#include <yandex_io/libs/audio_player/base/audio_player.h>
#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/threading/unique_callback.h>
#include <yandex_io/protos/quasar_proto.pb.h>
#include <yandex_io/sdk/private/device_context.h>

namespace quasar {
    class AudioEventDistributor: public AudioEventListener {
    public:
        AudioEventDistributor(std::shared_ptr<ipc::IServer> server, std::shared_ptr<YandexIO::IDevice> device);
        void onAudioEvent(const proto::AudioClientEvent& event) override;

    private:
        std::shared_ptr<YandexIO::IDevice> device_;
        std::shared_ptr<ipc::IServer> server_;
    };

    class AudioClientEndpoint {
    public:
        AudioClientEndpoint(std::shared_ptr<YandexIO::IDevice> device,
                            std::shared_ptr<ipc::IIpcFactory> ipcFactory,
                            std::shared_ptr<const IAudioClockManager> audioClockManager,
                            std::shared_ptr<AudioPlayerFactory> audioPlayerFactory,
                            std::shared_ptr<IMultiroomProvider> multiroomProvider,
                            std::shared_ptr<IStereoPairProvider> stereoPairProvider);
        ~AudioClientEndpoint();

        int getPort() const;

    private:
        void handleQuasarMessage(const ipc::SharedMessage& message);
        void handleSyncMessage(const ipc::SharedMessage& message);
        void clockTowerUpdate(const std::shared_ptr<ipc::IServer::IClientConnection>& connection, uint64_t hash);

    private:
        constexpr static const char* SERVICE_NAME = "audioclient";

        std::shared_ptr<YandexIO::IDevice> device_;
        std::shared_ptr<ipc::IServer> server_;
        std::shared_ptr<ipc::IConnector> syncdConnector_;
        std::shared_ptr<AudioEventDistributor> eventDistributor_;
        std::unique_ptr<AudioClientController> controller_;
        std::shared_ptr<const IAudioClockManager> audioClockManager_;
        YandexIO::DeviceContext deviceContext_;

        std::shared_ptr<NamedCallbackQueue> clockTowerQueue_;
        UniqueCallback clockTowerUpdate_;
    };
} // namespace quasar
