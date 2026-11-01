#pragma once

#include "draw_led_screen_directive_observer.h"
#include "led_devices_impl.h"
#include "frontal_animation/cache_manager.h"

#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/leds/led_manager/ng/animation_conductor.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_conductor.h>
#include <yandex_io/sdk/directive_observer.h>
#include <yandex_io/sdk/sdk_interface.h>
#include <yandex_io/modules/device_state/extended_device_state.h>

#include <set>

class LedDirectiveObserver: public YandexIO::DirectiveObserver, public YandexIO::BackendConfigObserver {
public:
    LedDirectiveObserver(std::shared_ptr<quasar::LedManager> ledManager,
                         std::shared_ptr<LedDevicesImpl> ledDevices,
                         std::shared_ptr<DrawLedScreenDirectiveObserver> drawLedScreenObserver,
                         std::shared_ptr<ClockDisplayStateHolder> clockDisplayStateHolder);
    ~LedDirectiveObserver() override;

    void subscribeToSystemConfig(YandexIO::SDKInterface& sdk);

    void onDirective(const std::string& name, const std::string& vinsRequestId, const std::string& jsonPayload) override;
    void onSystemConfig(const std::string& configName, const std::string& jsonPayload) override;
    void updateDeviceState(YandexIO::ExtendedDeviceState deviceState);

private:
    std::shared_ptr<AnimationComposition> getLocalComposition(const std::string& directiveName);
    std::shared_ptr<AnimationComposition> makeGifAnimationOverSpeaking(const std::string& gifName);

    void play(std::shared_ptr<DefaultAnimationConductor> conductor);

    void processDirective(const std::string& name);
    void updateDisabledDirectives(const std::string& jsonPayload);
    void updateCacheParameters(const std::string& jsonPayload);

private:
    std::shared_ptr<quasar::LedManager> ledManager_;
    std::shared_ptr<LedDevicesImpl> ledDevices_;
    std::shared_ptr<DrawLedScreenDirectiveObserver> drawLedScreenObserver_;
    std::shared_ptr<ClockDisplayStateHolder> clockDisplayStateHolder_;

    YandexIO::ExtendedDeviceState deviceState_;
    std::set<std::string> disabledDirectives_;
    quasar::NamedCallbackQueue callbackQueue_{"LedDirectiveObserver"};
};
