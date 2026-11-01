#include "rcu_button_event_message.h"

RcuButtonEventMessage::RcuButtonEventMessage(
    quasar::proto::ButtonEvent::Action action,
    rcu_button::Key buttonKey,
    bool isLongPress)
    : action_(action)
    , buttonKey_(buttonKey)
    , isLongPress_(isLongPress)
{
}

std::optional<RcuButtonEventMessage> RcuButtonEventMessage::fromQuasarMessage(
    const quasar::proto::QuasarMessage& message) {
    if (!message.has_rcu_event()) {
        return std::nullopt;
    }
    const auto& rcuEvent = message.rcu_event();

    if (!rcuEvent.has_button_event()) {
        return std::nullopt;
    }
    const auto& buttonEvent = rcuEvent.button_event();

    if (!buttonEvent.has_key_code()) {
        return std::nullopt;
    }
    const auto keyCode = buttonEvent.key_code();
    const auto buttonKey = rcu_button::findKey(keyCode);
    if (!buttonKey) {
        return std::nullopt;
    }

    if (!buttonEvent.has_action()) {
        return std::nullopt;
    }

    return RcuButtonEventMessage(
        buttonEvent.action(),
        *buttonKey,
        buttonEvent.is_long_press());
}

quasar::proto::ButtonEvent::Action RcuButtonEventMessage::getAction() const {
    return action_;
}

rcu_button::Key RcuButtonEventMessage::getButtonKey() const {
    return buttonKey_;
}

bool RcuButtonEventMessage::getIsLongPress() const {
    return isLongPress_;
}
