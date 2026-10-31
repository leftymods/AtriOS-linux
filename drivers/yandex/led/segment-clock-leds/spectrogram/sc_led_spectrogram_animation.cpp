#include "sc_led_spectrogram_animation.h"

ScLedSpectrogramAnimation::ScLedSpectrogramAnimation(
    std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory,
    std::weak_ptr<ScLedController> scLedController)
    : SpectrogramAnimation(std::move(ipcFactory), scLedController)
    , scLedController_(std::move(scLedController))
{
    updateLocalPresets("{}");
}

void ScLedSpectrogramAnimation::drawCurrentFrame()
{
    if (auto ledController = scLedController_.lock()) {
        ledController->drawFrame(frame_);
    }
}

void ScLedSpectrogramAnimation::updateSpectrogram(const Json::Value& /* presets */, const std::string& /* currentPreset */, const std::string& /* extraData */)
{
    spectrogram_ = std::make_unique<ScLedSpectrogram>();
}

std::shared_ptr<Spectrogram> ScLedSpectrogramAnimation::getSpectrogram()
{
    return spectrogram_;
}

void ScLedSpectrogramAnimation::clearSpectrogramScreen()
{
    frame_ = ScLedFrame::makeEmptyFrame();
}

void ScLedSpectrogramAnimation::fillSpectrogramScreen()
{
    frame_ = spectrogram_->frame();
}
