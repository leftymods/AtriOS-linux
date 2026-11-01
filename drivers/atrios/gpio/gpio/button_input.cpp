#include "button_input.h"

using namespace quasar;

ButtonInput::ButtonInput(int pinNumber, GPIO::Edge edge, bool inverted,
                         std::function<void()> onClick,
                         std::function<void()> onLongPress,
                         std::function<void(bool pressed)> onNewState)
    : pin_(pinNumber, GPIO::Direction::IN, edge)
    , onClick_(std::move(onClick))
    , onLongPress_(std::move(onLongPress))
    , onNewState_(std::move(onNewState))
    , pinNumber_(pinNumber)
{
    pressedState_ = inverted ? 0 : 1;
    unpressedState_ = 1 - pressedState_;
    lastState_ = pin_.read();
    lastStateStarted_ = std::chrono::steady_clock::now();
}

std::vector<const GPIO*> ButtonInput::getHandles() const {
    return {&pin_};
}

void ButtonInput::processEvent()
{
    int state = pin_.read();

    if (state == unpressedState_ && lastState_ == pressedState_)
    {
        if (!longPressCalled_ && onClick_)
        {
            onClick_();
        }
        longPressCalled_ = false;
    }

    auto now = std::chrono::steady_clock::now();
    if (state == pressedState_ && lastState_ == pressedState_ && std::chrono::duration_cast<std::chrono::milliseconds>(now - lastStateStarted_).count() >= LONG_PRESS_MS && !longPressCalled_)
    {
        if (onLongPress_)
        {
            onLongPress_();
            longPressCalled_ = true;
        }
    }
    if (state != lastState_)
    {
        lastState_ = state;
        lastStateStarted_ = std::chrono::steady_clock::now();
        if (onNewState_) {
            onNewState_(lastState_ == pressedState_);
        }
    }
}

void ButtonInput::reload()
{
    pin_ = GPIO(pinNumber_, GPIO::Direction::IN);
    pin_.read();
}

bool ButtonInput::isPressed() const {
    return lastState_ == pressedState_;
}
