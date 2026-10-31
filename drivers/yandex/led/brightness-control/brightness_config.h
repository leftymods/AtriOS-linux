#pragma once

#include <json/json.h>

#include <map>
#include <mutex>
#include <tuple>

class BrightnessConfig {
public:
    using LinearCurve = std::map<float, uint32_t>;

    explicit BrightnessConfig(LinearCurve curve, uint32_t fixedMin, uint32_t fixedMax);

    uint32_t getLevel(float sensorBrightness) const;

    void updateSystemConfig(const Json::Value& jsonConfig);

    void updateUserConfig(const Json::Value& jsonConfig);

private:
    static int mix(uint32_t first, uint32_t second, float ratio);

    void setFixedValue(std::optional<uint32_t> fixedValue);

    static bool isValidUserConfig(const Json::Value& jsonConfig);

private:
    const LinearCurve curve_;
    const uint32_t fixedMin_;
    const uint32_t fixedMax_;

    std::atomic<std::optional<uint32_t>> fixedValue_;
    std::atomic<bool> useUserConfig_ = false;
};
