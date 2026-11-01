#include "brightness_config.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <chrono>
#include <cmath>
#include <thread>

BrightnessConfig::BrightnessConfig(LinearCurve curve, uint32_t fixedMin, uint32_t fixedMax)
    : curve_(std::move(curve))
    , fixedMin_(fixedMin)
    , fixedMax_(fixedMax)
    , fixedValue_(std::nullopt)
{
}

uint32_t BrightnessConfig::getLevel(float sensorBrightness) const {
    if (auto value = fixedValue_.load()) {
        return *value;
    }

    auto nextLevel = curve_.upper_bound(sensorBrightness);
    LinearCurve::const_iterator level;

    if (nextLevel == curve_.begin()) {
        level = nextLevel;
    } else if (nextLevel == curve_.end()) {
        nextLevel--;
        level = nextLevel;
    } else {
        level = std::prev(nextLevel);
    }

    if (sensorBrightness < nextLevel->first) {
        float m = (sensorBrightness - level->first) / (nextLevel->first - level->first);
        return mix(level->second, nextLevel->second, m);
    } else {
        return nextLevel->second;
    }
}

int BrightnessConfig::mix(uint32_t first, uint32_t second, float ratio) {
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    return (first * (1 - ratio)) + (second * ratio);
}

void BrightnessConfig::updateSystemConfig(const Json::Value& jsonConfig) {
    if (useUserConfig_) {
        return; //user config trumps everything
    }
    int fixedValue = quasar::tryGetInt(jsonConfig, "fixedValue", -1);
    if (fixedValue == -1) {
        return;
    }
    setFixedValue(fixedValue);
}

void BrightnessConfig::setFixedValue(std::optional<uint32_t> fixedValue) {
    fixedValue_ = fixedValue;
}

void BrightnessConfig::updateUserConfig(const Json::Value& jsonConfig) {
    if (!isValidUserConfig(jsonConfig) || jsonConfig["auto"].asBool()) {
        useUserConfig_ = false;
        setFixedValue(std::nullopt);
        return;
    }

    useUserConfig_ = true;
    auto value = jsonConfig["value"].asFloat();

    uint32_t fixedValue = fixedMin_ + value * (fixedMax_ - fixedMin_);

    YIO_LOG_INFO("Setting current brightness to " << fixedValue);
    setFixedValue(fixedValue);
}

bool BrightnessConfig::isValidUserConfig(const Json::Value& jsonConfig) {
    if (!jsonConfig.isMember("auto")) {
        return false;
    }

    if (!jsonConfig.isMember("value") || !jsonConfig["value"].isDouble()) {
        return false;
    }

    int value = (int)(100.0 * jsonConfig["value"].asFloat());
    return value >= 0 && value <= 100;
}
