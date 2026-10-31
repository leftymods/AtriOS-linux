#include "clock_animation.h"

#include <yandex_io/libs/logging/logging.h>

ClockAnimation::ClockAnimation(std::weak_ptr<YandexStationFrontalController> ledDevice, FontConfig fontConfig)
    : ClockAnimationBase(ledDevice)
    , ledDevice_(std::move(ledDevice))
    , screen_({
          {YandexStationFrontalController::WIDTH,
           YandexStationFrontalController::HEIGHT},
          Frame(YandexStationFrontalController::HEIGHT * YandexStationFrontalController::WIDTH, {BGR_BLACK, BGR_BLACK, BGR_BLACK}),
      })
{
    setFontConfig(std::move(fontConfig));
}

void ClockAnimation::drawCurrentFrame() {
    if (!started()) {
        return;
    }

    std::scoped_lock lock(mutex_);

    draw(timeStr_, font_, {fontConfig_.paddingX, fontConfig_.paddingY}, screen_);

    if (auto ledController = ledDevice_.lock()) {
        ledController->setLastAnimationName("<clock>");
        ledController->drawBuffer(screen_.data);
    }
}

void ClockAnimation::setFontConfig(FontConfig config) {
    std::scoped_lock lock(mutex_);

    auto font = quasar::loadFont(config.fontFileName);

    fontConfig_ = std::move(config);
    font_ = std::move(font);

    std::fill(screen_.data.begin(), screen_.data.end(), BGR24::black);
}
