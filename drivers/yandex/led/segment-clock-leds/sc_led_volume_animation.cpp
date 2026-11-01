#include "sc_led_volume_animation.h"

#include <yandex_io/libs/logging/logging.h>

ScLedVolumeAnimation::ScLedVolumeAnimation(int currentVolume,
                                           int maxVolume,
                                           std::weak_ptr<ScLedController> ledControllerWeak)
    : ScLedPattern("volume.scled", std::move(ledControllerWeak))
{
    int volumeToDraw = (double)currentVolume / maxVolume * 100;
    if (volumeToDraw > 99) {
        volumeToDraw = 99;
    } else if (volumeToDraw < 0) {
        volumeToDraw = 0;
    }

    YIO_LOG_INFO("Volume to draw " << volumeToDraw);

    std::string strToDraw;
    if (volumeToDraw < 10) {
        strToDraw = " " + std::to_string(volumeToDraw);
    } else {
        strToDraw = std::to_string(volumeToDraw);
    }
    strToDraw += "    "; // 4 empty characters

    frames_.emplace_back(ScLedFrame::makeFrameFromString(strToDraw, ScLedFrame::brightnessOn, std::chrono::milliseconds(1000)));
}
