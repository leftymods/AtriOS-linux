#pragma once

#include <functional>
#include <string>

namespace wifi_firmware {
    enum class FwVer {
        UNSUPPORTED,
        DEFAULT_22,
        BACKPORT_29
    };

    FwVer getCurrentFirmware();
    std::string getLoadedFirmware();
    std::string getLoadedDriver();

    using WifiSwitcher = std::function<void(bool)>; // 'true' to turn wifi on, 'false' to turn wifi off

    // next functions returns true if version changed
    // it can be false in case if current firmware is target version already
    // or in case if system doesnt support firmware changing (property is empty)
    bool changeToDefault(WifiSwitcher wifiSwitcher);
    bool changeToBackport(WifiSwitcher wifiSwitcher);
} // namespace wifi_firmware
