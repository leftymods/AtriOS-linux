#pragma once

#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>

#include <yandex_io/modules/leds/led_manager/ng/animation.h>

class ProgressUpdateGif: public GifAnimation {
public:
    ProgressUpdateGif(std::weak_ptr<YandexStationFrontalController> ledDevice, const std::string& imageFilePath);
    ~ProgressUpdateGif() override = default;

    bool finished() const override;

    void setProgress(double progressPercents);

protected:
    FrameBufferOutputFrame getCurrentFrame() const override;

public:
    void updateTime(TimePoint timePoint) override;
    void startAnimationFrom(TimePoint timePoint) override;
    std::chrono::nanoseconds getLength() const override;

    mutable std::mutex mutex_;
};
