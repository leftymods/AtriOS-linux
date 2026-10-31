#pragma once

#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>

#include <queue>

bool equalsNear1(double volume1, double volume2);

class VolumeFrontalAnimation: public GifAnimation {
public:
    VolumeFrontalAnimation(const std::string& gifFilename,
                           std::weak_ptr<YandexStationFrontalController> ledDevice);

    void updateAnimation(double volume);

    void updateVolume(double volume);
    void updateTime(TimePoint timePoint) override;

private:
    bool finished() const override;

    void resetAnimation() override;

    FrameBufferOutputFrame getCurrentFrame() const override;

    mutable std::mutex frameQueueMutex_;
    std::queue<FrameBufferOutputFrame> frameQueue_;
    std::queue<int> durationQueue_;

    double volume_ = 0;
};
