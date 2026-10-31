#include "color_config.h"

#include <yandex_io/libs/logging/logging.h>

const float ColorConfig::EPSILON = 0.01f;

void ColorConfig::updateConfig(const Json::Value& jsonConfig, ColorConfig::DeviceExteriorColor deviceColor) {
    try {
        Json::Value gammaValue;

        if (deviceColor == DeviceExteriorColor::BLACK) {
            gammaValue = jsonConfig["black"]["gamma"];
        } else if (deviceColor == DeviceExteriorColor::WHITE) {
            gammaValue = jsonConfig["white"]["gamma"];
        }

        if (!gammaValue) {
            gammaValue = jsonConfig["default"]["gamma"];
        }

        gamma_ = gammaValue ? gammaValue.asFloat() : 1.0f;
    } catch (const Json::Exception& e) {
        YIO_LOG_WARN("error parsing json:" << e.what());
        gamma_ = 1.0f;
    }
}

std::vector<quasar::GifFrame> ColorConfig::applyCorrection(std::vector<quasar::GifFrame> gif, float gamma) const {
    float effectiveGamma = gamma * gamma_;
    if (abs(effectiveGamma - 1.0) > EPSILON) {
        for (auto& frame : gif) {
            applyCorrection(frame, effectiveGamma);
        }
    }
    return gif;
}

void ColorConfig::applyDeviceCorrection(quasar::Box& frame, float gamma) const {
    float effectiveGamma = gamma * gamma_;
    applyCorrection(frame, effectiveGamma);
}

void ColorConfig::applyCorrection(quasar::Box& frame, float gamma) {
    for (auto& pixel : frame.data) {
        pixel.b = pow((pixel.b / 255.0), gamma) * 255.0;
        pixel.g = pow((pixel.g / 255.0), gamma) * 255.0;
        pixel.r = pow((pixel.r / 255.0), gamma) * 255.0;
    }
}

ColorConfig::DeviceExteriorColor ColorConfig::getDeviceColor(const std::string& deviceId) {
    ColorConfig::DeviceExteriorColor deviceColor;
    if (deviceId[1] == 'K') {
        deviceColor = ColorConfig::DeviceExteriorColor::BLACK;
    } else if (deviceId[1] == 'W') {
        deviceColor = ColorConfig::DeviceExteriorColor::WHITE;
    } else {
        deviceColor = ColorConfig::DeviceExteriorColor::UNKNOWN;
    }
    return deviceColor;
}
