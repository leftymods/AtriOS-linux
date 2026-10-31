#pragma once

#include "yandex_station_frontal_controller.h"

#include <vector>

class YandexStationFrontalController; // circular

class FrameBufferOutputFrame {
public:
    explicit FrameBufferOutputFrame(Frame frame);
    FrameBufferOutputFrame();

    void draw(YandexStationFrontalController* controller) const;

private:
    Frame frame_;
};
