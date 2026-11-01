#pragma once

#include "led_devices_impl.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/config_utils.h>

#include <yandex_io/modules/demo_mode/led_handlers/demo_mode_led_handler.h>
#include <yandex_io/modules/leds/led_manager/led_manager.h>

class DemoModeFrontLedHandler: public DemoModeLedHandler {
public:
    DemoModeFrontLedHandler(std::shared_ptr<quasar::LedManager> ledManager,
                            std::shared_ptr<LedDevicesImpl> ledDevices,
                            const std::string& aliceFrontalAnimationFontsPath);
    void onDemoModeItemPlaying(const DemoItem& item) override;
    void onDemoModeItemFinished(const DemoItem& item, bool havePendingStories) override;

private:
    std::shared_ptr<AnimationComposition> getAnimationComposition(const std::string& filePath, bool speaking = false, bool looped = false) const;

private:
    const std::shared_ptr<LedDevicesImpl> ledDevices_;
    const FontConfig fontConfig_;
};
