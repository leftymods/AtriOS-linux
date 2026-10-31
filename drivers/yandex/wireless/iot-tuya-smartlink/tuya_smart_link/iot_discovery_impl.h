#pragma once

#include <yandex_io/libs/iot/i_iot_discovery.h>

#include <thread>

class IotDiscoveryImpl: public YandexIO::IIotDiscovery {
public:
    void start(const IIotDiscovery::PairingInfo& pairingInfo) override;
    void stop() noexcept override;

private:
    std::thread thread_;
};
