#pragma once

#include "gpio.h"
#include "gpio_input.h"

#include <chrono>
#include <functional>

namespace quasar {

    class ButtonInput: public GPIOInput {
    public:
        ButtonInput(int pinNumber, GPIO::Edge edge, bool inverted, std::function<void()> onClick,
                    std::function<void()> onLongPress = std::function<void()>(),
                    std::function<void(bool pressed)> onNewState = std::function<void(bool pressed)>());
        std::vector<const GPIO*> getHandles() const override;
        void processEvent() override;
        void reload() override;

        bool isPressed() const;

    private:
        GPIO pin_;
        int pressedState_ = 1;
        int unpressedState_ = 0;
        std::function<void()> onClick_;
        std::function<void()> onLongPress_;
        std::function<void(bool pressed)> onNewState_;
        int pinNumber_;

        std::chrono::steady_clock::time_point lastStateStarted_;
        int lastState_ = 0;
        bool longPressCalled_ = false;

        const int LONG_PRESS_MS = 4950;
    };

} // namespace quasar
