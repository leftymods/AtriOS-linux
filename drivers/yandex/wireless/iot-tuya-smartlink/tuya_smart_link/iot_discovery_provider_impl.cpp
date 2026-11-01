#include "iot_discovery_impl.h"
#include "iot_discovery_provider_impl.h"

std::shared_ptr<YandexIO::IIotDiscovery> IotDiscoveryProviderImpl::createDiscovery() {
    return std::make_shared<IotDiscoveryImpl>();
}
