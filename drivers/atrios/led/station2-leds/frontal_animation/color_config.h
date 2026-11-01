#pragma once

#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/gif_frame.h>

#include <json/json.h>

class ColorConfig {
public:
    enum class DeviceExteriorColor {
        BLACK,
        WHITE,
        UNKNOWN
    };

    ColorConfig() = default;

    void updateConfig(const Json::Value& jsonConfig, DeviceExteriorColor deviceColor);

    std::vector<quasar::GifFrame> applyCorrection(std::vector<quasar::GifFrame> gif, float gamma) const;

    void applyDeviceCorrection(quasar::Box& frame, float gamma) const;

    static ColorConfig::DeviceExteriorColor getDeviceColor(const std::string& deviceId);

private:
    static void applyCorrection(quasar::Box& frame, float gamma);

    static const float EPSILON;

    float gamma_ = 1.0f;
};
