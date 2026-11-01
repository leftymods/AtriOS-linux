#pragma once

#include "linux/i2c.h"

#include <smart_devices/libs/shared_memory/shared_memory.h>

#include <smart_devices/platforms/yandexstation_2/platformd/leds/framebuffer_led_controller.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/bgr24.h>

#include <yandex_io/modules/leds/led_controller/led_controller.h>
#include <yandex_io/modules/leds/led_controller/led_device.h>

#include <string>

// frontal leds
class YandexStationHeadController: public quasar::LedController {
public:
    static const int WIDTH;
    static const int HEIGHT;

    explicit YandexStationHeadController(std::string sysfsDeviceName);

    int getLedCount() const override;
    void drawFrame(const quasar::LedCircle& frame) override;
    quasar::rgbw_color readColor(const std::string& stringColor) override;

    YandexStationHeadController(const YandexStationHeadController& other) = delete;
    YandexStationHeadController(YandexStationHeadController&& _) = delete;

public:
    int getHeight() const override;

    int getWidth() const override;

    void clearFrame() override;

private:
    bool isInitialized_ = false;
    FramebufferLedController internalLedController_;
    YandexStationHeadController& operator=(const YandexStationHeadController& other) = delete;
};
