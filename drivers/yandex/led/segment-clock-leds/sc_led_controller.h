#pragma once

#include "sc_led_frame.h"

#include <yandex_io/modules/leds/led_controller/led_device.h>

class ScLedController: public quasar::LedDevice {
public:
    int getWidth() const override;
    int getHeight() const override;

    void clearFrame() override;

    virtual ~ScLedController() = default;

    virtual void drawFrame(ScLedFrame frame) = 0;
};
