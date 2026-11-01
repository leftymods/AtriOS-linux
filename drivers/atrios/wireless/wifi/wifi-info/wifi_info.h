#pragma once

#include <string>
#include <json/json.h>

#include <yandex_io/libs/device/i_device.h>

namespace quasar::net {
    /* if allBss parameter is true then "other_bss" field will be populated
       with available after scan non associated BSSes */
    struct WifiInfoSettings {
        bool allBss{false};
        bool iface{false};
        bool tids{false};
        bool bothIE{false};
        bool longCpblt{false};
        bool survey{false};
    };

    Json::Value getDetailedWifiInfo(const std::string& devName, const WifiInfoSettings& settings);

    struct WifiInfo {
        std::string ssid;
        std::string password;
    };

    WifiInfo getWifiInfo(const std::shared_ptr<YandexIO::IDevice>& device);

} // namespace quasar::net
