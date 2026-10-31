#include "firmware_switcher.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/android_support/system_property/system_property.h>
#include <yandex_io/libs/subprocess/subprocess.h>

#include <boost/algorithm/string/trim.hpp>
namespace {
    constexpr const char* DEFAULT_22_PROP_VALUE = "from.4.9";
    constexpr const char* BACKPORT_29_PROP_VALUE = "from.backport";
    constexpr const char* IWLWIFI_PROPERTY_NAME = "persist.vendor.iwlwifi.version";
    constexpr const char* FW_VERSION_SYS_PATH = "/sys/bus/pci/drivers/iwlwifi/0000:01:00.0/firmware_version";
    constexpr const char* DRV_VERSION_SYS_PATH = "/sys/bus/pci/drivers/iwlwifi/0000:01:00.0/driver_version";
    constexpr const char* SYS_22_FW_VALUE = "iwlwifi-7265D-22.ucode";
    constexpr const char* SYS_29_FW_VALUE = "iwlwifi-7265D-29.ucode";

    using FwVer = wifi_firmware::FwVer;

    void setFirmware(FwVer ver) {
        std::string propVal;
        if (ver == FwVer::DEFAULT_22) {
            propVal = DEFAULT_22_PROP_VALUE;
        } else if (ver == FwVer::BACKPORT_29) {
            propVal = BACKPORT_29_PROP_VALUE;
        };
        YIO_LOG_DEBUG("Trying to switch to " << propVal);
        if (!propVal.empty()) {
            android_support::setSystemProperty(IWLWIFI_PROPERTY_NAME, propVal.c_str());
        }
    }

    FwVer loadedToFwVer(const std::string& sysValue) {
        if (sysValue == SYS_22_FW_VALUE) {
            return FwVer::DEFAULT_22;
        }
        if (sysValue == SYS_29_FW_VALUE) {
            return FwVer::BACKPORT_29;
        }
        return FwVer::UNSUPPORTED;
    }

    void loggedSystem(const std::string& cmd) {
        YIO_LOG_DEBUG("exec " << cmd);
        system(cmd.c_str());
    }

    void rmmodList(std::initializer_list<std::string> lst) {
        for (auto name : lst) {
            loggedSystem("rmmod " + std::string(name));
        }
    }

    void insmodListPath(const std::string path, std::initializer_list<std::string> lst) {
        for (auto name : lst) {
            loggedSystem("insmod " + path + name);
        }
    }

    void unloadOldDriver() {
        rmmodList({"iwlmvm", "iwlwifi", "mac80211", "cfg80211"});
    }

    void unloadNewDriver() {
        rmmodList({"compat", "iwlmvm", "iwlwifi", "mac80211", "cfg80211"});
    }

    void loadNewDriver() {
        insmodListPath("/vendor/lib/backported_iwlwifi/",
                       {"compat.ko", "cfg80211.ko", "mac80211.ko", "iwlwifi.ko", "iwlmvm.ko"});
    }

    void loadOldDriver() {
        insmodListPath("/vendor/lib/modules/",
                       {"cfg80211.ko", "mac80211.ko", "iwlwifi.ko", "iwlmvm.ko"});
    }

    void unloadDriver(FwVer ver) {
        loggedSystem("svc wifi disable");
        switch (ver) {
            case FwVer::DEFAULT_22:
                unloadOldDriver();
                break;
            case FwVer::BACKPORT_29:
                unloadNewDriver();
                break;
            default:
                break;
        }
        loggedSystem("stop wificond");
        loggedSystem("stop android.hardware.wifi@1.0-service");
    }

    void loadDriver(FwVer ver) {
        switch (ver) {
            case FwVer::DEFAULT_22:
                loadOldDriver();
                break;
            case FwVer::BACKPORT_29:
                loadNewDriver();
                break;
            default:
                break;
        }
        loggedSystem("start android.hardware.wifi@1.0-service");
        loggedSystem("start wificond");
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    bool changeFirwmare(FwVer fromVersion, FwVer toVersion, wifi_firmware::WifiSwitcher& wifiSwitcher) {
        if (wifi_firmware::getCurrentFirmware() == fromVersion) {
            setFirmware(toVersion);
            const auto result = wifi_firmware::getCurrentFirmware() == toVersion;
            if (!wifiSwitcher) {
                return result;
            }
            if (const auto loaded = loadedToFwVer(wifi_firmware::getLoadedFirmware()); loaded != FwVer::UNSUPPORTED && loaded != toVersion) {
                wifiSwitcher(false);
                unloadDriver(loaded);
                loadDriver(toVersion);
                wifiSwitcher(true);
                auto result = quasar::getSystemCommandOutputTail("svc", {"wifi", "enable"}, 1000); // doesn't work via 'system()' call, probably needs stdout/stderr
                YIO_LOG_DEBUG("svc wifi enable: stdout = '" << result.stdOut << "', err = '" << result.stdErr << "'");
            }
            return result;
        }
        return false;
    }

} // namespace

FwVer wifi_firmware::getCurrentFirmware() {
    auto strVal = android_support::getSystemProperty(IWLWIFI_PROPERTY_NAME);
    YIO_LOG_DEBUG("Current firmware is " << strVal);
    if (strVal == DEFAULT_22_PROP_VALUE) {
        return FwVer::DEFAULT_22;
    }
    if (strVal == BACKPORT_29_PROP_VALUE) {
        return FwVer::BACKPORT_29;
    }
    return FwVer::UNSUPPORTED;
}

std::string wifi_firmware::getLoadedDriver() {
    try {
        return boost::trim_copy(quasar::getFileContent(DRV_VERSION_SYS_PATH));
    } catch (...) {
    }
    return "unknown";
}

std::string wifi_firmware::getLoadedFirmware() {
    try {
        return boost::trim_copy(quasar::getFileContent(FW_VERSION_SYS_PATH));
    } catch (...) {
    }
    return "unknown";
}

bool wifi_firmware::changeToDefault(WifiSwitcher wifiSwitcher) {
    return changeFirwmare(FwVer::BACKPORT_29, FwVer::DEFAULT_22, wifiSwitcher);
}

bool wifi_firmware::changeToBackport(WifiSwitcher wifiSwitcher) {
    return changeFirwmare(FwVer::DEFAULT_22, FwVer::BACKPORT_29, wifiSwitcher);
}
