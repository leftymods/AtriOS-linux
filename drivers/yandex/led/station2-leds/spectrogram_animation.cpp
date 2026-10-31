#include "spectrogram_animation.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/spectrogram/spectrogram_factory.h>

namespace {

    const int WIDTH = YandexStationFrontalController::WIDTH;
    const int HEIGHT = YandexStationFrontalController::HEIGHT;

} // namespace

YandexStation2SpectrogramAnimation::YandexStation2SpectrogramAnimation(
    std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory,
    std::weak_ptr<YandexStationFrontalController> ledDevice)
    : SpectrogramAnimation(std::move(ipcFactory), ledDevice)
    , ledDevice_(std::move(ledDevice))
    , screen_({
          {WIDTH,
           HEIGHT},
          Frame(HEIGHT * WIDTH, {BGR_BLACK, BGR_BLACK, BGR_BLACK}),
      })
{
    updateLocalPresets("{}");
}

void YandexStation2SpectrogramAnimation::drawCurrentFrame() {
    if (auto ledController = ledDevice_.lock()) {
        ledController->drawBuffer(screen_.data);
    }
}

void YandexStation2SpectrogramAnimation::updateSpectrogram(const Json::Value& presets, const std::string& currentPreset, const std::string& extraData) {
    spectrogram_ = createSpectrogram(WIDTH, HEIGHT, presets[currentPreset], presets, extraData);
}

std::shared_ptr<Spectrogram> YandexStation2SpectrogramAnimation::getSpectrogram() {
    return spectrogram_;
}

void YandexStation2SpectrogramAnimation::clearSpectrogramScreen() {
    std::fill(screen_.data.begin(), screen_.data.end(), BGR24{BGR_BLACK, BGR_BLACK, BGR_BLACK});
}

void YandexStation2SpectrogramAnimation::fillSpectrogramScreen() {
    for (size_t y = 0; y < HEIGHT; y++) {
        for (size_t x = 0; x < WIDTH; x++) {
            float cf = std::clamp(spectrogram_->pixel(x, y), 0.0f, 1.0f);

            std::uint8_t c = std::floor(std::pow(256.0f, cf)) - 1;

            putPixel(x, (HEIGHT - 1) - y, {c, c, c}, screen_);
        }
    }
}
