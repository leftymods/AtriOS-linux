#include "gif_animation.h"
#include "frontal_animation/frame_buffer_output_frame.h"

#include <yandex_io/libs/logging/logging.h>

#include <iostream>

std::shared_ptr<GifAnimation> GifAnimation::readGif(const std::string& filepath,
                                                    std::weak_ptr<YandexStationFrontalController> ledDevice) {
    return std::make_shared<GifAnimation>(std::move(ledDevice), filepath, quasar::loadGif(filepath));
}

quasar::Gif GifAnimation::gammaCorrectFrames(quasar::Gif gif,
                                             const std::string& filepath,
                                             const std::weak_ptr<YandexStationFrontalController>& ledDevice) {
    if (auto sharedLedDevice = ledDevice.lock()) {
        float iGamma = 1.0;
        std::fstream gammaFile(filepath + ".gamma", std::ios_base::in);
        if (gammaFile.good()) {
            gammaFile >> iGamma;
        }
        gif = sharedLedDevice->getColorConfig().applyCorrection(std::move(gif), iGamma);
    }
    return gif;
}

GifAnimation::GifAnimation(std::weak_ptr<YandexStationFrontalController> ledDevice, std::string filepath, quasar::Gif gif)
    : Animation(ledDevice)
    , filepath_(std::move(filepath))
{
    gif = gammaCorrectFrames(gif, filepath_, ledDevice);
    ledDevice_ = std::move(ledDevice);
    frames_.resize(gif.size());
    durationCentiSeconds_.resize(gif.size());
    setName(filepath_);
    unsigned long nimage;
    for (nimage = 0; nimage < gif.size(); nimage++) {
        setFrame(nimage, std::move(gif[nimage].data), gif[nimage].durationCentiSeconds);
    }
    YIO_LOG_DEBUG("Loaded " << nimage << " from " << filepath_)
}

GifAnimation::GifAnimation(std::weak_ptr<YandexStationFrontalController> ledDevice, const std::string& filepath)
    : GifAnimation(ledDevice, filepath, quasar::loadGif(filepath))
{
}

GifAnimation::GifAnimation(std::weak_ptr<YandexStationFrontalController> ledDevice, int count)
    : Animation(ledDevice)
    , ledDevice_(std::move(ledDevice))
    , frames_(count)
    , durationCentiSeconds_(count) // 1/100 of second
{
}

GifAnimation::GifAnimation(std::weak_ptr<YandexStationFrontalController> ledDevice)
    : GifAnimation(std::move(ledDevice), 0)
{
}

void GifAnimation::drawCurrentFrame() {
    if (!started_) {
        YIO_LOG_WARN("not started! not good " << name_);
        return;
    }

    if (finished()) {
        YIO_LOG_WARN("finished! not good " << name_);
        return;
    }

    auto frame = getCurrentFrame();
    if (auto ledController = ledDevice_.lock()) {
        if (currentFrame_ == 0) {
            YIO_LOG_DEBUG("Draw first frame of " << getName() << " GifAnimation")
            ledController->setLastAnimationName(getName());
        }
        frame.draw(ledController.get());
    }
}

bool GifAnimation::finished() const {
    return currentFrame_ > (frames_.size() - 1);
}

Animation::TimePoint GifAnimation::getEndOfFrameTimePoint() const {
    return endOfFrame_;
}

std::string GifAnimation::getName() const {
    return name_;
}

void GifAnimation::resetAnimation() {
    started_ = false;
    currentFrame_ = 0;
}

void GifAnimation::setFrame(int index, Frame frame, int timeCentiSeconds) {
    frames_[index] = FrameBufferOutputFrame(std::move(frame));
    durationCentiSeconds_[index] = timeCentiSeconds;
}

FrameBufferOutputFrame GifAnimation::getCurrentFrame() const {
    return frames_[currentFrame_];
}

void GifAnimation::setName(std::string name) {
    name_ = std::move(name);
}

int GifAnimation::getFrameCount() const {
    return frames_.size();
}

int GifAnimation::currentFrame() const {
    return currentFrame_;
}

std::string GifAnimation::getFilepath() const {
    return filepath_;
}

void GifAnimation::updateTime(Animation::TimePoint timePoint) {
    if (!started_) {
        return;
    }
    auto delta = timePoint - startOfCurrentFrame_;
    int droppedFrames = -1;
    while (!finished() && delta >= getCurrentFrameDuration()) {
        auto currentFrameDuration = getCurrentFrameDuration();
        startOfCurrentFrame_ += currentFrameDuration;
        delta -= currentFrameDuration;
        currentFrame_ += 1;
        droppedFrames += 1;
        if (!finished()) {
            endOfFrame_ = startOfCurrentFrame_ + getCurrentFrameDuration();
        }
    }

    if (droppedFrames > 0) {
        if (auto ledController = ledDevice_.lock()) {
            ledController->addDroppedFrames(droppedFrames);
        }
    }
}

inline std::chrono::milliseconds GifAnimation::getCurrentFrameDuration() {
    return std::chrono::milliseconds(durationCentiSeconds_[currentFrame_] * 10);
}

void GifAnimation::startAnimationFrom(Animation::TimePoint timePoint) {
    if (started_) {
        return;
    }

    started_ = true;
    startTime_ = timePoint;
    startOfCurrentFrame_ = timePoint;
    currentFrame_ = 0;
    // endOfDuration is handled separately
    if (!finished()) {
        endOfFrame_ = timePoint + std::chrono::milliseconds(durationCentiSeconds_[currentFrame_] * 10);
    }
}

std::chrono::nanoseconds GifAnimation::getLength() const {
    auto length = std::chrono::nanoseconds(0);

    for (const auto& centiSeconds : durationCentiSeconds_) {
        length += std::chrono::milliseconds(centiSeconds * 10);
    }
    return length;
}
