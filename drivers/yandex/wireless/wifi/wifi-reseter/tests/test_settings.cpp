#include "../settings.h"

#include <yandex_io/libs/json_utils/json_utils.h>

#include <library/cpp/testing/unittest/registar.h>

#include <iostream>

const char* SETTINGS_JSON_STRING =
    R"JSON({
"minTx": {
  "intervalSec": 10,
  "limit": 11,
  "method": "old"
},
"minRx": {
  "intervalSec": 12,
  "limit": 13,
  "method": "hard"
},
"maxTxErrors": {
  "intervalSec": 14,
  "limit": 15,
  "method": "abracadabra"
},
"maxRxErrors": {
  "intervalSec": 16,
  "limit": 17
},
"noInternet": {
   "intervalSec": 22,
   "method": "old"
},
"autoReset": {
 "intervalSec": 33,
 "method": "hard"
},
"dailyReset": {
  "time": "10:23",
  "method": "old"
},
"sixClicksEnabled": true,
"minResetIntervalSec": 1380
})JSON";

Y_UNIT_TEST_SUITE(WifiReseterSettings) {
    Y_UNIT_TEST(basic) {
        using Method = WifiReseterSettings::Method;
        const auto cfg = quasar::parseJson(SETTINGS_JSON_STRING);
        auto settings = wifiReseterSettingsFromJson(cfg);

        auto check = [](const auto& src, std::chrono::seconds interval, unsigned limit, Method method) {
            UNIT_ASSERT(src.interval == interval);
            UNIT_ASSERT_EQUAL(src.limit, limit);
            UNIT_ASSERT(src.method == method);
        };

        check(settings.minTx, std::chrono::seconds(10), 11, Method::OLD);
        check(settings.minRx, std::chrono::seconds(12), 13, Method::HARD);
        check(settings.maxTxErrors, std::chrono::seconds(14), 15, Method::UNSET);
        check(settings.maxRxErrors, std::chrono::seconds(16), 17, Method::UNSET);
        UNIT_ASSERT(settings.sixClicksEnabled);

        auto check2 = [](const auto& src, auto interval, Method method) {
            UNIT_ASSERT(src.value().interval == interval);
            UNIT_ASSERT(src.value().method == method);
        };

        check2(settings.noInternet, std::chrono::seconds(22), Method::OLD);
        check2(settings.autoReset, std::chrono::seconds(33), Method::HARD);
        check2(settings.dailyReset, std::chrono::minutes(10 * 60 + 23), Method::OLD);
        UNIT_ASSERT(settings.noInternet.value().method == Method::OLD);
        UNIT_ASSERT(settings.autoReset.value().interval == std::chrono::seconds(33));
        UNIT_ASSERT(settings.autoReset.value().method == Method::HARD);
        UNIT_ASSERT(settings.minResetInterval == std::chrono::minutes(23));
    }

    Y_UNIT_TEST(parseTime) {
        auto parse = [](const std::string& src) {
            return WifiReseterSettings::parseTime(src);
        };
        UNIT_ASSERT(!parse(""));
        UNIT_ASSERT(!parse("  :  "));
        UNIT_ASSERT(!parse("25:01"));
        UNIT_ASSERT(!parse("00:60"));
        UNIT_ASSERT(parse("01:01").value() == std::chrono::hours(1) + std::chrono::minutes(1));
    }
}
