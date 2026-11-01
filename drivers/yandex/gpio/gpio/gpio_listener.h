#pragma once

#include "gpio.h"
#include "gpio_input.h"

#include <functional>

namespace quasar {

    class GPIOListener: public GPIOInput {
    public:
        GPIOListener(int pinNumber, std::function<void(bool value)> onValueChanged = nullptr);
        std::vector<const GPIO*> getHandles() const override;
        void processEvent() override;
        void reload() override;

    private:
        GPIO pin_;
        std::function<void(bool value)> onValueChanged_;
        int pinNumber_;
        bool lastValue_ = false;
    };

} // namespace quasar
