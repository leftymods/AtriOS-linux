#pragma once

#include "sc_led_pattern.h"

class ScLedVolumeAnimation: public ScLedPattern {
public:
    ScLedVolumeAnimation(int currentVolume, int maxVolume, std::weak_ptr<ScLedController> ledControllerWeak);

private:
    std::string volumeStr_;
};
