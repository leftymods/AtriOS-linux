#pragma once

#include <smart_devices/platforms/yandexstation_2/platformd/rcu_button_event_message.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/draw_utils.h>

#include <chrono>

class ForegroundApp {
public:
    [[nodiscard]] virtual std::string getName() const = 0;

    virtual void drawCurrentFrame(quasar::Screen& screen) = 0;

    virtual bool handleRcu(const RcuButtonEventMessage& rcuMessage) = 0;

    virtual ~ForegroundApp() = default;
};
