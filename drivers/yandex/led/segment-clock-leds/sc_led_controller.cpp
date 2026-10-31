#include "sc_led_controller.h"

int ScLedController::getWidth() const {
    return getLedCount();
}

int ScLedController::getHeight() const {
    return 1;
}

void ScLedController::clearFrame() {
    auto emptyFrame = ScLedFrame::makeEmptyFrame();
    drawFrame(std::move(emptyFrame));
}
