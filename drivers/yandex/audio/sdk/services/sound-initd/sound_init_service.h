#pragma once

#include "sound_init_endpoint.h"

#include <yandex_io/interfaces/device_state/i_device_state_provider.h>
#include <yandex_io/libs/base/quasar_service.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/sdk/sdk_interface.h>

namespace quasar {
    class SoundInitService: public QuasarService {
    public:
        SoundInitService(std::shared_ptr<YandexIO::IDevice> device,
                         std::shared_ptr<ipc::IIpcFactory> ipcFactory,
                         std::shared_ptr<IDeviceStateProvider> deviceStateProvider,
                         std::shared_ptr<YandexIO::SDKInterface> sdk);

        std::string getServiceName() const override;
        std::string getThreadName() const override;

    private:
        void start() override;

        std::shared_ptr<YandexIO::IDevice> device_;
        std::shared_ptr<ipc::IIpcFactory> ipcFactory_;
        std::shared_ptr<IDeviceStateProvider> deviceStateProvider_;
        std::unique_ptr<SoundInitEndpoint> soundInitEndpointPtr_;
        const std::shared_ptr<YandexIO::SDKInterface> sdk_;
    };
} // namespace quasar
