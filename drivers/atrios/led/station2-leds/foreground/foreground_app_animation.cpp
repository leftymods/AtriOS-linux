#include "foreground_app_animation.h"

namespace {

    constexpr std::chrono::milliseconds INTER_FRAMES_PAUSE = std::chrono::milliseconds(40);

}

ForegroundAppAnimation::ForegroundAppAnimation(
    std::weak_ptr<YandexStationFrontalController> ledDevice,
    std::shared_ptr<ForegroundApp> app)
    : Animation(ledDevice)
    , ledDevice_(std::move(ledDevice))
    , app_(std::move(app))
    , screen_({
          {YandexStationFrontalController::WIDTH,
           YandexStationFrontalController::HEIGHT},
          Frame(
              YandexStationFrontalController::HEIGHT *
                  YandexStationFrontalController::WIDTH,
              {BGR_BLACK, BGR_BLACK, BGR_BLACK}),
      })
    , endOfFrame_(std::chrono::steady_clock::now())
{
}

void ForegroundAppAnimation::drawCurrentFrame() {
    if (auto ledController = ledDevice_.lock()) {
        ledController->drawBuffer(screen_.data);
    }
}

bool ForegroundAppAnimation::finished() const {
    return false;
}

Animation::TimePoint ForegroundAppAnimation::getEndOfFrameTimePoint() const {
    return endOfFrame_;
}

std::string ForegroundAppAnimation::getName() const {
    return app_->getName();
}

void ForegroundAppAnimation::resetAnimation() {
}

void ForegroundAppAnimation::updateTime(Animation::TimePoint timePoint) {
    while (timePoint >= endOfFrame_) {
        endOfFrame_ = endOfFrame_ + INTER_FRAMES_PAUSE;
    }
}

void ForegroundAppAnimation::startAnimationFrom(Animation::TimePoint timePoint) {
    endOfFrame_ = timePoint + INTER_FRAMES_PAUSE;
}

std::chrono::nanoseconds ForegroundAppAnimation::getLength() const {
    return INTER_FRAMES_PAUSE;
}
