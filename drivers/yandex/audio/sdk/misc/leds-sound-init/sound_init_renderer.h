#pragma once

#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/leds/led_manager/ng/stoppable_animation_conductor.h>

#include <yandex_io/sdk/device_mode_observer.h>

#include <memory>

/**
 * @brief DeviceModeObserver implementation that draw SoundInit led animations via ledManager
 */
class SoundInitRenderer final: public YandexIO::DeviceModeObserver {
public:
    explicit SoundInitRenderer(std::shared_ptr<quasar::LedManager> ledManager);
    ~SoundInitRenderer() = default;

    void onStartSetup(bool /*isFirstSetup*/) override{};

    void onSoundDataTransferStart() override;
    void onConnectingToNetwork() override;
    void onSetupError() override;
    void onFinishSetup() override;

    void onConfigureSuccessUpdate(DeviceModeObserver::ConfigurationSuccessState /*configurationSuccessState*/) override{};

private:
    std::shared_ptr<quasar::LedManager> ledManager_;
    std::shared_ptr<StoppableAnimationConductor> currentConductor_;
};
