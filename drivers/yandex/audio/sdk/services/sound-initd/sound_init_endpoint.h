#pragma once

#include <yandex_io/capabilities/file_player/interfaces/i_file_player_capability.h>

#include <yandex_io/interfaces/device_state/i_device_state_provider.h>

#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/setup_parser/sound_data_receiver.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>

#include <yandex_io/protos/quasar_proto.pb.h>

#include <yandex_io/sdk/audio_source/i_audio_source_client.h>
#include <yandex_io/sdk/private/device_context.h>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quasar {

    class SoundInitEndpoint {
    public:
        static const std::string SERVICE_NAME;

        SoundInitEndpoint(
            std::shared_ptr<YandexIO::IDevice> device,
            std::shared_ptr<ipc::IIpcFactory> ipcFactory,
            std::shared_ptr<IDeviceStateProvider> deviceStateProvider,
            std::shared_ptr<YandexIO::IAudioSourceClient> audioSource,
            std::shared_ptr<YandexIO::IFilePlayerCapability> filePlayerCapability);
        ~SoundInitEndpoint();

        void handleQuasarMessage(const ipc::SharedMessage& message);

        void waitUntilInited();

    private:
        Lifetime lifetime_;
        std::shared_ptr<YandexIO::IAudioSourceClient::Listener> ioAudioSourceListener_;
        std::shared_ptr<YandexIO::IAudioSourceClient> ioAudioSource_;
        std::shared_ptr<YandexIO::IDevice> device_;
        std::shared_ptr<ipc::IIpcFactory> ipcFactory_;
        SoundDataReceiver soundDataReceiver_;
        std::shared_ptr<ipc::IConnector> setupdConnector_;
        quasar::SteadyConditionVariable connectionsWakeUpVar_;
        std::shared_ptr<const YandexIO::LatencyData> soundInitStartLatencyPoint_;
        const std::shared_ptr<YandexIO::IFilePlayerCapability> filePlayerCapability_;
        std::mutex mutex_;

        YandexIO::RequestChannelType channelType_ = YandexIO::RequestChannelType::MAIN;

        bool initMode_ = false;
        bool ignoreSound_ = false;

        YandexIO::DeviceContext deviceContext_;

        void fireError(const std::string& wav);

        void startInitMode();

        void stopInitMode();

        void onDataReceived(const std::vector<unsigned char>& payload, int protocolVersion);

        void onUnsupportedProtocol(int version);

        void onTransferStart();

        void onTransferError();

        void onSetupCompleted();
    };

} /* namespace quasar */
