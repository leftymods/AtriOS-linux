#include "foreground_activity.h"

#include <smart_devices/platforms/yandexstation_2/platformd/rcu_button_event_message.h>
#include <smart_devices/platforms/yandexstation_2/platformd/rcu_button.h>

#include <yandex_io/libs/base/directives.h>

#include <chrono>

namespace {

    constexpr std::chrono::milliseconds NO_ACTIONS_DISABLE_PAUSE = std::chrono::minutes(5);
    constexpr std::chrono::milliseconds NO_ACTIONS_DISABLE_CHECK_PERIOD = std::chrono::seconds(10);

}

ForegroundActivity::ForegroundActivity(
    std::weak_ptr<YandexStationFrontalController> ledDevice,
    std::shared_ptr<ForegroundApp> app)
    : app_(std::move(app))
    , animation_(std::make_shared<ForegroundAppAnimation>(std::move(ledDevice), app_))
    , isActive_(false)
    , isActiveSignal_(std::make_shared<ForegroundActivity::IsActiveSignal>())
    , lastActionTimestamp_(std::chrono::steady_clock::now())
{
    disableIfNeedChecker_ = std::make_unique<quasar::PeriodicExecutor>(
        std::bind(&ForegroundActivity::disableIfNeed, this),
        NO_ACTIONS_DISABLE_CHECK_PERIOD,
        quasar::PeriodicExecutor::PeriodicType::SLEEP_FIRST);
}

void ForegroundActivity::setIsActive(bool isActive) {
    std::scoped_lock guard(mutex_);
    if (isActive == isActive_) {
        return;
    }
    if (isActive) {
        updateLastActionTimestamp();
    }
    isActive_ = isActive;
    isActiveSignal_->operator()(isActive);
}

void ForegroundActivity::disableIfNeed() {
    const auto now = std::chrono::steady_clock::now();
    if (lastActionTimestamp_ + NO_ACTIONS_DISABLE_PAUSE > now) {
        return;
    }
    setIsActive(false);
}

void ForegroundActivity::updateLastActionTimestamp() {
    lastActionTimestamp_ = std::chrono::steady_clock::now();
}

std::shared_ptr<ForegroundActivity::IsActiveSignal> ForegroundActivity::isActiveSignal() const {
    return isActiveSignal_;
}

std::shared_ptr<ForegroundAppAnimation> ForegroundActivity::getAnimation() const {
    return animation_;
}

bool ForegroundActivity::handleQuasarMessage(const quasar::ipc::SharedMessage& message) {
    std::scoped_lock guard(mutex_);

    if (!isActive_) {
        return false;
    }

    const auto rcuMessage = RcuButtonEventMessage::fromQuasarMessage(*message);
    if (!rcuMessage) {
        return false;
    }

    updateLastActionTimestamp();

    if (app_->handleRcu(*rcuMessage)) {
        return true;
    }

    const auto action = rcuMessage->getAction();
    const auto buttonKey = rcuMessage->getButtonKey();
    if (buttonKey == rcu_button::Key::BACK && action == quasar::proto::ButtonEvent::UP) {
        setIsActive(false);
    }
    return true;
}

bool ForegroundActivity::isActive() const {
    return isActive_;
}

void ForegroundActivity::onDirective(const std::string& name, const std::string&, const std::string&) {
    if (name != quasar::Directives::START_FOREGROUND_ACTIVITY) {
        return;
    }
    setIsActive(true);
}
