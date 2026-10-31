#include "rcu_volume_frontal_animation.h"

#include <yandex_io/libs/logging/logging.h>

#include <cmath>
#include <sstream>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

namespace {
    int convertToAliceVolume(double volume) {
        if (volume < 0.1 && volume > 0) {
            return 1;
        } else {
            return std::round(volume * 10);
        }
    }
} // namespace

RcuVolumeFrontalAnimation::RcuVolumeFrontalAnimation(std::string aliceGifLedAnimations,
                                                     std::weak_ptr<YandexStationFrontalController> yandexStationFrontalController)
    : frontalLedController_(std::move(yandexStationFrontalController))
    , aliceGifLedAnimationsPath_(std::move(aliceGifLedAnimations))
    , rcuVolumeAnimationsPath_(aliceGifLedAnimationsPath_ + "rcu_volume/")
{
}

void RcuVolumeFrontalAnimation::updateAnimation(double volume, bool muted, bool showPulse) {
    std::scoped_lock guard(mutex_);
    int aliceVolume = convertToAliceVolume(volume);
    YIO_LOG_DEBUG("Update animation. "
                  << "Old volume " << currentVolume_ << ", new volume " << volume
                  << ". Old alice volume " << currentAliceVolume_ << ", new alice volume " << aliceVolume);
    selectNewAnimation(volume, aliceVolume, muted, showPulse);
    currentVolume_ = volume;
    currentAliceVolume_ = aliceVolume;
}

void RcuVolumeFrontalAnimation::updateVolume(double volume) {
    std::scoped_lock guard(mutex_);
    int aliceVolume = convertToAliceVolume(volume);
    YIO_LOG_DEBUG("Update volume. "
                  << "Old volume " << currentVolume_ << ", new volume " << volume
                  << ". Old alice volume " << currentAliceVolume_ << ", new alice volume " << aliceVolume);
    currentVolume_ = volume;
    currentAliceVolume_ = aliceVolume;
}

std::shared_ptr<Animation> RcuVolumeFrontalAnimation::getCurrentAnimation() {
    std::scoped_lock guard(mutex_);
    if (shouldUpdateCurrentAnimation()) {
        YIO_LOG_DEBUG("Update volume animation");
        currentAnimation_ = newAnimation_;
        // since we are changing animation "under the hood", we need take some functions of LedAnimator
        if (currentAnimation_) {
            currentAnimation_->startAnimationFrom(std::chrono::steady_clock::now());
        }
        currentAnimationType_ = newAnimationType_;
        newAnimationType_ = AnimationType::UNDEFINED;
        newAnimation_ = nullptr;
    }
    return currentAnimation_;
}

bool RcuVolumeFrontalAnimation::shouldUpdateCurrentAnimation() const {
    if (newAnimation_ == nullptr || newAnimationType_ == AnimationType::UNDEFINED) {
        return false;
    }

    // If last animation is null or finished load new animation
    if (currentAnimation_ == nullptr || currentAnimation_->finished() ||
        currentAnimationType_ == AnimationType::UNDEFINED) {
        return true;
    }

    // If last animation is not finished yet, skip edge animation
    if (newAnimationType_ == AnimationType::EDGE && currentAnimationType_ != AnimationType::MUTE) {
        return false;
    }

    // Don't interrupt rotation animation with the pulse animation
    if (newAnimationType_ == AnimationType::PULSE && currentAnimationType_ == AnimationType::ROTATION) {
        return false;
    }

    return true;
}

void RcuVolumeFrontalAnimation::selectNewAnimation(double volume, int aliceVolume, bool muted, bool showPulse) {
    std::string animationPath;
    if (muted) {
        // If muted show mute animation
        newAnimationType_ = AnimationType::MUTE;
        animationPath = aliceGifLedAnimationsPath_ + "sound_mute.gif";
    } else if (volume == currentVolume_ && (aliceVolume == 0 || aliceVolume == 10)) {
        // If volume has not changed and this is 0 or 10, show edge animation
        newAnimationType_ = AnimationType::EDGE;
        animationPath = selectVolumeEdge(aliceVolume);
    } else if (currentAliceVolume_ != -1 && abs(aliceVolume - currentAliceVolume_) == 1) {
        // If alice volume has changed show transition
        newAnimationType_ = AnimationType::ROTATION;
        animationPath = volume < currentVolume_ ? selectVolumeRotationDown(aliceVolume)
                                                : selectVolumeRotationUp(aliceVolume);
    } else if (showPulse) {
        newAnimationType_ = AnimationType::PULSE;
        animationPath = volume < currentVolume_ ? selectVolumePulseDown(aliceVolume) : selectVolumePulseUp(aliceVolume);
    } else {
        newAnimationType_ = AnimationType::UNDEFINED;
    }
    if (newAnimationType_ == AnimationType::UNDEFINED || animationPath.empty()) {
        newAnimation_ = nullptr;
    } else if (newAnimation_ == nullptr || animationPath != newAnimation_->getFilepath()) {
        YIO_LOG_DEBUG("Load new volume animation: " << animationPath);
        try {
            newAnimation_ = GifAnimation::readGif(animationPath, frontalLedController_);
        } catch (const std::exception& e) {
            newAnimation_ = nullptr;
            YIO_LOG_ERROR_EVENT("RcuVolumeFrontalAnimation.ReadGifFailed", "failed to draw on led screen: " << e.what());
        }
    }
}

std::string RcuVolumeFrontalAnimation::selectVolumeEdge(int aliceVolume) const {
    Y_VERIFY(aliceVolume == 0 || aliceVolume == 10);
    std::stringstream gifPath;
    gifPath << rcuVolumeAnimationsPath_ << "volume_edge/" << aliceVolume << ".gif";
    return gifPath.str();
}

std::string RcuVolumeFrontalAnimation::selectVolumePulseUp(int aliceVolume) const {
    std::stringstream gifPath;
    gifPath << rcuVolumeAnimationsPath_ << "volume_pulse/up/" << aliceVolume << ".gif";
    return gifPath.str();
}

std::string RcuVolumeFrontalAnimation::selectVolumePulseDown(int aliceVolume) const {
    std::stringstream gifPath;
    gifPath << rcuVolumeAnimationsPath_ << "volume_pulse/down/" << aliceVolume << ".gif";
    return gifPath.str();
}

std::string RcuVolumeFrontalAnimation::selectVolumeRotationUp(int aliceVolume) const {
    Y_VERIFY(aliceVolume > 0);
    std::stringstream gifPath;
    gifPath << rcuVolumeAnimationsPath_ << "volume_rotation/up/" << aliceVolume - 1 << ".gif";
    return gifPath.str();
}

std::string RcuVolumeFrontalAnimation::selectVolumeRotationDown(int aliceVolume) const {
    Y_VERIFY(aliceVolume < 10);
    std::stringstream gifPath;
    gifPath << rcuVolumeAnimationsPath_ << "volume_rotation/down/" << aliceVolume + 1 << ".gif";
    return gifPath.str();
}
