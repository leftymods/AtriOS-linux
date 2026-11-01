#pragma once

#include "led_devices_impl.h"

#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/sdk/backend_config_observer.h>
#include <yandex_io/sdk/media_observer.h>

#include <memory>

/**
 * MediaStateLedPlayer implementation that will draw local media gif resulting from RCU commands
 */
class MediaStateLedPlayer: public YandexIO::MediaObserver, public YandexIO::BackendConfigObserver {
public:
    MediaStateLedPlayer(std::shared_ptr<quasar::LedManager> ledManager,
                        std::shared_ptr<LedDevicesImpl> ledDevices);

    void onMediaError(ContentType contentType) override;
    void onMediaPaused(ContentType contentType) override;
    void onMediaSwitchedForward(ContentType contentType) override;
    void onMediaSwitchedBackward(ContentType contentType) override;
    void onMediaLiked(ContentType contentType) override;
    void onMediaDisliked(ContentType contentType) override;
    void onMediaResumed(ContentType contentType) override;

    void onMediaRequest(ContentType /* contentType */) override {
    }
    void onMediaStarted(ContentType /* contentType */) override {
    }

    void onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) override;

private:
    void playFrontalAnimation(const std::string& animation);

private:
    std::shared_ptr<quasar::LedManager> ledManager_;
    std::shared_ptr<LedDevicesImpl> ledDevices_;
    bool disableSwitchTrackAnimation_;
};
