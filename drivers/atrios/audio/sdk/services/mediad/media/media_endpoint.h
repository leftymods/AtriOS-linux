#pragma once

#include "players_controller.h"

#include <yandex_io/services/mediad/media/players/player_factory.h>

#include <yandex_io/capabilities/file_player/interfaces/i_file_player_capability.h>

#include <yandex_io/interfaces/auth/i_auth_provider.h>
#include <yandex_io/interfaces/device_state/i_device_state_provider.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/signals/signal.h>
#include <yandex_io/sdk/private/device_context.h>

#include <memory>
#include <mutex>

namespace quasar {

    class MediaEndpoint {
    public:
        MediaEndpoint(
            std::shared_ptr<YandexIO::IDevice> device,
            std::shared_ptr<ipc::IIpcFactory> ipcFactory,
            std::shared_ptr<quasar::IAuthProvider> authProvider,
            std::shared_ptr<IDeviceStateProvider> deviceStateProvider,
            std::shared_ptr<const PlayerFactory> playerFactory,
            std::shared_ptr<YandexIO::IDeviceStateCapability> deviceState,
            std::shared_ptr<YandexIO::IFilePlayerCapability> filePlayerCapability);
        ~MediaEndpoint();

        static const std::string SERVICE_NAME;

    private:
        void handleQuasarMessage(const ipc::SharedMessage& message);
        void handleMediaRequest(const proto::MediaRequest& mediaRequest);
        void notifyOnPlaybackError(const std::string& vinsRequestId);
        void sendGoHome();

    private:
        Lifetime lifetime_;
        const std::shared_ptr<YandexIO::IDevice> device_;
        const std::shared_ptr<quasar::IAuthProvider> authProvider_;
        std::shared_ptr<ipc::IServer> server_;

        std::atomic<std::chrono::seconds> abortOnFreezeTimeout_{std::chrono::seconds(15)};
        std::atomic<std::chrono::seconds> errorSoundTimeout_{std::chrono::seconds(30)};
        std::atomic<std::chrono::seconds> errorSoundDebounce_{std::chrono::seconds(10)};
        std::atomic<bool> playbackSoundError_{true};
        std::atomic<bool> playbackLedError_{true};

        std::atomic<bool> isBrick_{false};
        std::atomic<int> onAuthErrorCounter_{0};

        std::shared_ptr<PlayersController> playersController_;
        YandexIO::DeviceContext deviceContext_;
        std::string passportUid_;

        std::chrono::time_point<std::chrono::steady_clock> lastMediaRequest_;
        std::chrono::time_point<std::chrono::steady_clock> lastErrorSound_;

        std::shared_ptr<ipc::IConnector> toSyncd_;
        std::shared_ptr<ipc::IConnector> toBrickd_;
        std::shared_ptr<ipc::IConnector> toAliced_;

        const std::shared_ptr<YandexIO::IDeviceStateCapability> deviceState_;
        const std::shared_ptr<YandexIO::IFilePlayerCapability> filePlayerCapability_;
    };

} // namespace quasar
