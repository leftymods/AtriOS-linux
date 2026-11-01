#pragma once

#include "frontal_animation/frame_buffer_output_frame.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/gif_utils.h>

#include <yandex_io/modules/leds/led_manager/ng/animation.h>

#include <memory>

class GifAnimation: public Animation {
public:
    GifAnimation(std::weak_ptr<YandexStationFrontalController> ledDevice, int count);

    GifAnimation(std::weak_ptr<YandexStationFrontalController> ledDevice, std::string filepath, quasar::Gif gif);

    GifAnimation(std::weak_ptr<YandexStationFrontalController> ledDevice, const std::string& filepath);

    explicit GifAnimation(std::weak_ptr<YandexStationFrontalController> ledDevice);
    ~GifAnimation() override = default;

    static std::shared_ptr<GifAnimation> readGif(const std::string& filepath,
                                                 std::weak_ptr<YandexStationFrontalController> ledDevice);

    void drawCurrentFrame() override;

    bool finished() const override;

    TimePoint getEndOfFrameTimePoint() const override;

    std::string getName() const override;

    void resetAnimation() override;

    void setFrame(int index, Frame frame, int timeCentiSeconds);

    void setName(std::string name);

    int getFrameCount() const;

    int currentFrame() const;

    std::string getFilepath() const;
    void updateTime(TimePoint timePoint) override;
    void startAnimationFrom(TimePoint timePoint) override;
    std::chrono::nanoseconds getLength() const override;

private:
    std::weak_ptr<YandexStationFrontalController> ledDevice_;

protected:
    virtual FrameBufferOutputFrame getCurrentFrame() const;
    std::chrono::time_point<std::chrono::steady_clock> endOfFrame_;
    std::vector<FrameBufferOutputFrame> frames_;

    // per gif standard length of frame is in 1/100 of second
    std::vector<int> durationCentiSeconds_;
    unsigned int currentFrame_ = 0;
    TimePoint startOfCurrentFrame_;
    bool started_ = false;

private:
    std::string name_;
    const std::string filepath_;

    static quasar::Gif gammaCorrectFrames(quasar::Gif gif, const std::string& filepath, const std::weak_ptr<YandexStationFrontalController>& ledDevice);

    // TODO: remove?
    TimePoint startTime_;
    std::chrono::milliseconds getCurrentFrameDuration();
};
