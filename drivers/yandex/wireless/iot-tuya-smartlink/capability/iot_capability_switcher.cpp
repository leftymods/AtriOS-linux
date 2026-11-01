#include "iot_capability_switcher.h"
#include "iot_capability.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

namespace YandexIO {

    IotCapabilitySwitcher::IotCapabilitySwitcher(
        std::shared_ptr<SDKInterface> sdk,
        std::shared_ptr<IDevice> device,
        std::weak_ptr<IIotCapabilityEvents> events,
        std::shared_ptr<IEndpoint> localEndpoint)
        : sdk_(std::move(sdk))
        , device_(std::move(device))
        , events_(std::move(events))
        , localEndpoint_(std::move(localEndpoint))
    {
    }

    void IotCapabilitySwitcher::onIotCapabilityEnabled(bool enabled)
    {
        if (enabled == iotCapabilityEnabled_) {
            return;
        }

        YIO_LOG_INFO("onIotCapabilityEnabled: " << enabled);

        if (enabled) {
            iotCapability_ = std::make_shared<IotCapability>(sdk_, device_, events_, device_->hal()->createIotDiscoveryProvider());
            localEndpoint_->addCapability(iotCapability_);
        } else {
            localEndpoint_->removeCapability(iotCapability_);
            iotCapability_.reset();

            if (auto sevents = events_.lock()) {
                sevents->onIotCapabilityOff();
            }
        }

        iotCapabilityEnabled_ = enabled;
    }

    void IotCapabilitySwitcher::onEndpointAdded(const std::shared_ptr<IEndpoint>& /*endpoint*/)
    {
        // do nothing
    }

    void IotCapabilitySwitcher::onEndpointRemoved(const std::shared_ptr<IEndpoint>& /*endpoint*/)
    {
        // do nothing
    }

    void IotCapabilitySwitcher::onCapabilityConfigChanged(const quasar::proto::CapabilityConfig& config)
    {
        onIotCapabilityEnabled(config.iot_capability_enabled());
    }

} // namespace YandexIO
