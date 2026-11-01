#include "rcu_buttons_controller.h"

#include <yandex_io/libs/base/utils.h>

using namespace YandexIO;

RcuButtonsController::RcuButtonsController(std::shared_ptr<YandexIO::SDKInterface> sdk)
    : sdk_(std::move(sdk))
{
}

void RcuButtonsController::onSDKState(const YandexIO::SDKState& state)
{
    std::scoped_lock guard(mutex_);
    deviceState_ = state;
}

bool RcuButtonsController::handleNavLeftPress()
{
    sdk_->getPlaybackControlCapability()->prev();
    return true;
}

bool RcuButtonsController::handleNavRightPress()
{
    sdk_->getPlaybackControlCapability()->next();
    return true;
}

bool RcuButtonsController::handleMainPress()
{
    sdk_->getPlaybackControlCapability()->togglePlayPause(false);
    return true;
}

bool RcuButtonsController::handleBackPress()
{
    std::scoped_lock guard(mutex_);
    switch (deviceState_.aliceState.state) {
        case SDKState::AliceState::State::SPEAKING:
        case SDKState::AliceState::State::LISTENING:
        case SDKState::AliceState::State::THINKING:
        case SDKState::AliceState::State::SHAZAM:
            sdk_->getAliceCapability()->stopConversation();
            return true;
        default:
            return false;
            ;
    }

    return false;
}
