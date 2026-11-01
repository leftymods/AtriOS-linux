#pragma once

#include "iot_capability.h"

#include <yandex_io/libs/device/device.h>
#include <yandex_io/sdk/backend_config_observer.h>
#include <yandex_io/sdk/sdk_interface.h>
#include <yandex_io/sdk/private/endpoint_storage/endpoint.h>
#include <yandex_io/sdk/interfaces/i_endpoint_storage.h>
#include <smart_devices/libs/iot/capability/interface/i_iot_capability_events.h>

#include <memory>

namespace YandexIO {

    class IotCapabilitySwitcher: public IEndpointStorage::IListener {
    public:
        IotCapabilitySwitcher(
            std::shared_ptr<SDKInterface> sdk,
            std::shared_ptr<IDevice> device,
            std::weak_ptr<IIotCapabilityEvents> events,
            std::shared_ptr<IEndpoint> localEndpoint);

    private:
        void onIotCapabilityEnabled(bool enabled);

        void onEndpointAdded(const std::shared_ptr<IEndpoint>& endpoint) override;
        void onEndpointRemoved(const std::shared_ptr<IEndpoint>& endpoint) override;
        void onCapabilityConfigChanged(const quasar::proto::CapabilityConfig& config) override;

    private:
        bool iotCapabilityEnabled_ = false;
        std::shared_ptr<IotCapability> iotCapability_;
        const std::shared_ptr<SDKInterface> sdk_;
        const std::shared_ptr<IDevice> device_;
        const std::weak_ptr<IIotCapabilityEvents> events_;
        const std::shared_ptr<IEndpoint> localEndpoint_;
    };

} // namespace YandexIO
