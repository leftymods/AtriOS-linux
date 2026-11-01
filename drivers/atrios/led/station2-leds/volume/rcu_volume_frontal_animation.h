#pragma once

#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/frontal_animation/yandex_station_frontal_controller.h>

#include <yandex_io/modules/leds/led_manager/ng/animation.h>

#include <mutex>

class RcuVolumeFrontalAnimation {
public:
    RcuVolumeFrontalAnimation(std::string aliceGifLedAnimations,
                              std::weak_ptr<YandexStationFrontalController> yandexStationFrontalController);

    void updateAnimation(double volume, bool muted, bool showPulse);

    void updateVolume(double volume);

    std::shared_ptr<Animation> getCurrentAnimation();

private:
    enum class AnimationType {
        UNDEFINED,
        PULSE,
        ROTATION,
        EDGE,
        MUTE
    };

    bool shouldUpdateCurrentAnimation() const;

    void selectNewAnimation(double volume, int aliceVolume, bool muted, bool showPulse);

    std::string selectVolumeEdge(int aliceVolume) const;

    std::string selectVolumePulseUp(int aliceVolume) const;

    std::string selectVolumePulseDown(int aliceVolume) const;

    std::string selectVolumeRotationUp(int aliceVolume) const;

    std::string selectVolumeRotationDown(int aliceVolume) const;

    std::weak_ptr<YandexStationFrontalController> frontalLedController_;
    std::shared_ptr<GifAnimation> currentAnimation_;
    AnimationType currentAnimationType_{AnimationType::UNDEFINED};
    std::shared_ptr<GifAnimation> newAnimation_;
    AnimationType newAnimationType_{AnimationType::UNDEFINED};
    const std::string aliceGifLedAnimationsPath_;
    const std::string rcuVolumeAnimationsPath_;
    int currentAliceVolume_{-1};
    double currentVolume_{0};
    std::mutex mutex_;
};
