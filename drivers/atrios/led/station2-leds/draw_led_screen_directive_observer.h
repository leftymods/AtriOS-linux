#pragma once

#include "led_devices_impl.h"
#include "frontal_animation/cache_manager.h"

#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/device/i_device.h>
#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/leds/led_manager/ng/animation_conductor.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_conductor.h>

class DrawLedScreenDirectiveObserver {
public:
    using EventListener = std::function<void()>;

    enum class State {
        PLAYING,
        STOPPED
    };

    DrawLedScreenDirectiveObserver(std::shared_ptr<quasar::LedManager> ledManager, std::shared_ptr<LedDevicesImpl> ledDevices, std::shared_ptr<YandexIO::IDevice> device);
    ~DrawLedScreenDirectiveObserver();

    void onDrawLedScreenDirective(const std::string& name, const std::string& jsonPayload);
    void setEventListener(DrawLedScreenDirectiveObserver::EventListener eventListener);
    bool isPlaying();
    const std::shared_ptr<DefaultAnimationConductor>& getActiveConductor() const;
    void stop();
    bool tillEndOfSpeech() const;

    void setSizeEntries(unsigned long sizeEntries);
    void setSizeKbytes(int sizeKbytes);

private:
    void play(std::shared_ptr<DefaultAnimationConductor> conductor);
    static bool hasNoCacheHeader(const std::vector<std::pair<std::string, std::string>>& header);
    void processDirective(const std::string& name, const std::string& jsonPayload);

private:
    const std::shared_ptr<YandexIO::IDevice> device_;
    std::shared_ptr<quasar::LedManager> ledManager_;
    std::shared_ptr<LedDevicesImpl> ledDevices_;
    CacheManager cacheManager_;
    std::atomic<State> state_ = State::STOPPED;
    std::shared_ptr<DefaultAnimationConductor> activeConductor_;
    EventListener eventListener_;
    quasar::NamedCallbackQueue callbackQueue_{"DrawLedScreenDirectiveObserver"};
    std::optional<std::shared_ptr<DefaultAnimationConductor>> extractConductor(const std::string& jsonPayload);

    bool tillEndOfSpeech_;
};
