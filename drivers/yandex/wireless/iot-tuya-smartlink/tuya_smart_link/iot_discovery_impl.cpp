#include "iot_discovery_impl.h"

#include <smart_devices/third_party/tuya_sdk/tuya_smart_link.h>

#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("iot");

void IotDiscoveryImpl::start(const IIotDiscovery::PairingInfo& pairingInfo) {
    YIO_LOG_INFO("Iot discovery start");
    if (thread_.joinable()) {
        YIO_LOG_ERROR_EVENT("IotDiscoveryImpl.DiscoveryAlreadyStarted", "Discovery already started, restarting");
        stop();
    }
    thread_ = std::thread([pairingInfo]() {
        tuya_smart_link(pairingInfo.wifiSSID.c_str(), pairingInfo.cipher.c_str(), pairingInfo.wifiPassword.c_str(),
                        pairingInfo.token.c_str(), 5, 2, 1000, 1, 1);
    });
}

void IotDiscoveryImpl::stop() noexcept {
    YIO_LOG_INFO("Iot discovery stop");
    send_status_stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}
