#include "settings.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <charconv>

std::optional<std::chrono::minutes> WifiReseterSettings::parseTime(const std::string& src) {
    // clang-format off
    if (src.size() == 5
        && std::isdigit(src[0])
        && std::isdigit(src[1])
        && src[2] == ':'
        && std::isdigit(src[3])
        && std::isdigit(src[4])) { //clang-format on
        int hours = 0;
        int minutes = 0;
        std::from_chars(src.begin(), std::next(src.begin(), 2), hours);
        std::from_chars(std::next(src.begin(), 3), src.end(), minutes);
        if (hours < 24 && minutes < 60) {
            return std::chrono::minutes(std::chrono::hours(hours)) + std::chrono::minutes(minutes);
        }
    }
    return std::nullopt;
}

namespace {
    using Method = WifiReseterSettings::Method;
    using SecondsInterval = WifiReseterSettings::SecondsInterval;

    WifiReseterSettings::Method methodFromString(const std::string& str) {
        if (str == "hard") {
            return Method::HARD;
        }
        if (str == "old") {
            return Method::OLD;
        }
        return Method::UNSET;
    }
    
    WifiReseterSettings::Method methodFromJson(const Json::Value& obj) {
        return methodFromString(obj["method"].asString());
    }
} // namespace

WifiReseterSettings wifiReseterSettingsFromJson(const Json::Value& cfg) {
    auto applySeconds = [](auto& dst, const auto& src, const std::string& name) {
        if (src.isMember(name)) {
            dst = std::chrono::seconds(src[name].asInt());
        }
    };

    auto applySecondsInterval = [](auto& dst, const auto& cfg, const std::string& name) {
        if (cfg.isMember(name)) {
            const auto& src = cfg[name];
            if (src.isMember("intervalSec") && src.isMember("method")) {
                SecondsInterval tmp {
                    .interval = std::chrono::seconds(src["intervalSec"].asInt()),
                    .method = methodFromJson(src)
                };
                if (tmp.interval != std::chrono::seconds(0) && tmp.method != Method::UNSET) {
                    dst = tmp;
                }
            }
        }
    };

    auto applyTrigger = [&applySeconds, &cfg](auto& dst, const std::string& name) {
        if (cfg.isMember(name)) {
            const auto& src = cfg[name];
            
            applySeconds(dst.interval, src, "intervalSec");
            dst.limit = quasar::tryGetInt(src, "limit", 0);
            dst.method = methodFromJson(src);
        }
    };

    WifiReseterSettings newSettings;

    //count/interval
    applyTrigger(newSettings.minTx, "minTx");
    applyTrigger(newSettings.minRx, "minRx");
    applyTrigger(newSettings.maxTxErrors, "maxTxErrors");
    applyTrigger(newSettings.maxRxErrors, "maxRxErrors");

    newSettings.sixClicksEnabled = quasar::tryGetBool(cfg, "sixClicksEnabled", false);

    applySecondsInterval(newSettings.noInternet, cfg, "noInternet");
    applySecondsInterval(newSettings.autoReset, cfg, "autoReset");
    applySeconds(newSettings.minResetInterval, cfg, "minResetIntervalSec");

    if (cfg.isMember("dailyReset")) {
        const auto& src = cfg["dailyReset"];
        if (src.isMember("time")) {
            if (const auto& timeStr = src["time"]; timeStr.isString()) {
                auto when = WifiReseterSettings::parseTime(timeStr.asString());
                if (when.has_value()) {
                    if (const auto method = methodFromJson(src); method != Method::UNSET) {
                        newSettings.dailyReset = SecondsInterval {
                            .interval = when.value(),
                            .method = method
                        };
                    }
                }
            }
        }
    }
    return newSettings;
}
