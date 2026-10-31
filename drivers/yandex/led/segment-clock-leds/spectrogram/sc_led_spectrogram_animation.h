#pragma once

#include <smart_devices/libs/spectrogram/spectrogram_animation.h>
#include <smart_devices/libs/segment_clock_leds/sc_led_controller.h>

#include "sc_led_spectrogram.h"

class ScLedSpectrogramAnimation: public SpectrogramAnimation {
public:
    ScLedSpectrogramAnimation(
        std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory,
        std::weak_ptr<ScLedController> scLedController);
    ~ScLedSpectrogramAnimation() = default;

public:
    void drawCurrentFrame() override;

protected:
    void updateSpectrogram(const Json::Value& /* presets */, const std::string& /* currentPreset */, const std::string& /* extraData */) override;
    std::shared_ptr<Spectrogram> getSpectrogram() override;
    void clearSpectrogramScreen() override;
    void fillSpectrogramScreen() override;

private:
    std::weak_ptr<ScLedController> scLedController_;
    ScLedFrame frame_;
    std::shared_ptr<ScLedSpectrogram> spectrogram_;
};
