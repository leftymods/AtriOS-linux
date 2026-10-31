#pragma once

#include <yandex_io/libs/iot/i_iot_discovery.h>
#include <yandex_io/libs/iot/i_iot_discovery_provider.h>

class IotDiscoveryProviderImpl: public YandexIO::IIotDiscoveryProvider {
public:
    std::shared_ptr<YandexIO::IIotDiscovery> createDiscovery() override;
};
