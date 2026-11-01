#pragma once

#include "frontal_animation/yandex_station_frontal_controller.h"

class YandexStation2LedDevices {
public:
    virtual std::shared_ptr<YandexStationFrontalController> getLedScreen() = 0;
};
