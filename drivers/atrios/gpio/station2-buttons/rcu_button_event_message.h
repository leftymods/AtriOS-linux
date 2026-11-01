#pragma once

#include "rcu_button.h"
#include <yandex_io/protos/quasar_proto.pb.h>

#include <optional>

class RcuButtonEventMessage {
public:
    static std::optional<RcuButtonEventMessage> fromQuasarMessage(
        const quasar::proto::QuasarMessage& message);

    [[nodiscard]] quasar::proto::ButtonEvent::Action getAction() const;

    [[nodiscard]] rcu_button::Key getButtonKey() const;

    [[nodiscard]] bool getIsLongPress() const;

private:
    RcuButtonEventMessage(
        quasar::proto::ButtonEvent::Action action,
        rcu_button::Key buttonKey,
        bool isLongPress);

private:
    quasar::proto::ButtonEvent::Action action_;
    rcu_button::Key buttonKey_;
    bool isLongPress_;
};