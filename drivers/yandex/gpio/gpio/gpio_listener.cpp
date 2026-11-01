#include "gpio_listener.h"

using namespace quasar;

GPIOListener::GPIOListener(int pinNumber, std::function<void(bool value)> onValueChanged)
    : pin_(pinNumber, GPIO::Direction::IN)
    , onValueChanged_(std::move(onValueChanged))
    , pinNumber_(pinNumber)
    , lastValue_(pin_.read() != 0)
{
}

std::vector<const GPIO*> GPIOListener::getHandles() const {
    return {&pin_};
}

void GPIOListener::processEvent() {
    const bool state = pin_.read() != 0;

    if (state != lastValue_) {
        onValueChanged_(state);
        lastValue_ = state;
    }
}

void GPIOListener::reload() {
    pin_ = GPIO(pinNumber_, GPIO::Direction::IN);
    pin_.read();
}
