#pragma once

#include "audioclient/audio_client_endpoint.h"
#include "media/media_endpoint.h"

#include <yandex_io/services/mediad/media/players/player_factory.h>

#include <yandex_io/sdk/sdk_interface.h>

#include <yandex_io/interfaces/auth/i_auth_provider.h>
#include <yandex_io/interfaces/device_state/i_device_state_provider.h>
#include <yandex_io/interfaces/multiroom/i_multiroom_provider.h>
#include <yandex_io/interfaces/stereo_pair/i_stereo_pair_provider.h>
#include <yandex_io/libs/base/quasar_service.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>

#include <memory>
#include <string>

namespace quasar {
    class MediaService: public QuasarService {
    public:
        MediaService(
            std::shared_ptr<YandexIO::IDevice> device,
            std::shared_ptr<ipc::IIpcFactory> ipcFactory,
            std::shared_ptr<quasar::IAuthProvider> authProvider,
            std::shared_ptr<IDeviceStateProvider> deviceStateProvider,
            std::shared_ptr<IMultiroomProvider> multiroomProvider,
            std::shared_ptr<IStereoPairProvider> stereoPairProvider,
            std::shared_ptr<const PlayerFactory> mediaPlayerFactory,
            std::shared_ptr<YandexIO::SDKInterface> sdk);

        void start() override;
        std::string getServiceName() const override;

    private:
        void handleSyncMessage(const ipc::SharedMessage& message);

    private:
        const std::shared_ptr<YandexIO::IDevice> device_;
        const std::shared_ptr<ipc::IIpcFactory> ipcFactory_;
        const std::shared_ptr<quasar::IAuthProvider> authProvider_;
        const std::shared_ptr<IDeviceStateProvider> deviceStateProvider_;
        const std::shared_ptr<IMultiroomProvider> multiroomProvider_;
        const std::shared_ptr<IStereoPairProvider> stereoPairProvider_;
        const std::shared_ptr<const PlayerFactory> mediaPlayerFactory_;
        const std::shared_ptr<YandexIO::SDKInterface> sdk_;

        std::shared_ptr<MediaEndpoint> mediaEndpoint_;
        std::unique_ptr<AudioClientEndpoint> audioClientEndpoint_;

        std::shared_ptr<ipc::IConnector> syncdConnector_;
    };
} // namespace quasar
