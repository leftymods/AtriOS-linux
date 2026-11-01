#pragma once

//
// © YANDEX LLC, 2021
//

#pragma once

#include <string>

namespace YandexIO {
    struct WifiInfo {
        enum class WifiType {
            WEP = 0,
            WPA = 1,
            OPEN = 2,
            UNKNOWN_WIFI_TYPE = 3,
            NONE_WIFI_TYPE = 4
        };

        std::string ssid;
        int32_t rssi;
        std::string mac;
        int32_t freq;
        int32_t channelWidth;
        WifiType wifiType;
        bool isConnected = false;
        bool isCorporate = false;
    };
} /* namespace YandexIO */
