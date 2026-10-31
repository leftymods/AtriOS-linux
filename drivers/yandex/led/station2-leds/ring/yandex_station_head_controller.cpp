#include "yandex_station_head_controller.h"

#include <yandex_io/libs/errno/errno_exception.h>

#include <yandex_io/modules/leds/led_controller/color_readers.h>

#include <utility>

using namespace quasar;

const int YandexStationHeadController::WIDTH = 24;
const int YandexStationHeadController::HEIGHT = 1;

YandexStationHeadController::YandexStationHeadController(std::string sysfsDeviceName)
    : internalLedController_(std::move(sysfsDeviceName), YandexStationHeadController::HEIGHT, YandexStationHeadController::WIDTH)
{
    isInitialized_ = internalLedController_.initFramebuffer();
}

void YandexStationHeadController::drawFrame(const quasar::LedCircle& frame) {
    if (!isInitialized_) {
        return;
    }
    auto copyTo = internalLedController_.getFb().get();
    for (auto pixel = frame.rbegin(); pixel != frame.rend(); ++pixel) {
        copyTo->r = pixel->r;
        copyTo->g = pixel->g;
        copyTo->b = pixel->b;
        copyTo++;
    }
    internalLedController_.panFrame();
}

rgbw_color YandexStationHeadController::readColor(const std::string& stringColor) {
    return readRGBColor(stringColor);
}

int YandexStationHeadController::getLedCount() const {
    return internalLedController_.getLedCount();
}

int YandexStationHeadController::getHeight() const {
    return internalLedController_.getHeight();
}

int YandexStationHeadController::getWidth() const {
    return internalLedController_.getWidth();
}

void YandexStationHeadController::clearFrame() {
    if (isInitialized_) {
        internalLedController_.clearFrame();
    }
}
