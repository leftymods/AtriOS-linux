#pragma once

#include "i_light_sensor.h"

#include <string>

class LightSensor final
    : public ILightSensor {
public:
    enum class HardwareGain {
        X1 = 1,
        X3 = 3,
        X6 = 6,
        X9 = 9,
        X18 = 18
    };

    enum class IntegrationTime {
        T25MS = 25,
        T50MS = 50,
        T100MS = 100,
        T200MS = 200,
        T400MS = 400
    };

public:
    LightSensor(
        const std::string& deviceName,
        HardwareGain gain = HardwareGain::X18,
        IntegrationTime time = IntegrationTime::T400MS);

    void setHardwareGain(HardwareGain gain);

    void setIntegrationTime(IntegrationTime time);

    float getBrightness() override;

    ~LightSensor() = default;

private:
    static constexpr float DEFAULT_BRIGHTNESS = 10.0f;
    const std::string devicePath_;
};
