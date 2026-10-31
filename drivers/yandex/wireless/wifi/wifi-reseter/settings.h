#pragma once

#include <chrono>
#include <optional>

#include <yandex_io/libs/triggers/time_trigger.h>
#include <json/json.h>

struct WifiReseterSettings {
    std::chrono::seconds minResetInterval = std::chrono::minutes(15);

    enum Method {
        UNSET,
        OLD,
        HARD,
    };

    struct SecondsInterval {
        std::chrono::seconds interval;
        Method method{Method::UNSET};
    };

    std::optional<SecondsInterval> noInternet;
    std::optional<SecondsInterval> autoReset;  // will reset over this period after previous reset or start if set
    std::optional<SecondsInterval> dailyReset; // will reset at this minute from beginig of the day according local time

    struct TriggerConfig: public quasar::TriggerConfig {
        Method method{Method::UNSET};
    };

    TriggerConfig minTx;
    TriggerConfig minRx;
    TriggerConfig maxRxErrors;
    TriggerConfig maxTxErrors;

    bool sixClicksEnabled{false};

    //! parse minutes since start of the day from string in "HH:MM" format
    static std::optional<std::chrono::minutes> parseTime(const std::string& /*src*/);
};

WifiReseterSettings wifiReseterSettingsFromJson(const Json::Value& /*json*/);
