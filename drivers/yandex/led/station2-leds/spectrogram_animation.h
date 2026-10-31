#pragma once

#include "frontal_animation/yandex_station_frontal_controller.h"

#include <smart_devices/libs/spectrogram/spectrogram_animation.h>

#include <smart_devices/platforms/yandexstation_2/platformd/leds/spectrogram/spectrogram.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/draw_utils.h>

class YandexStation2SpectrogramAnimation: public SpectrogramAnimation {
public:
    YandexStation2SpectrogramAnimation(
        std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory,
        std::weak_ptr<YandexStationFrontalController> ledDevice);

    ~YandexStation2SpectrogramAnimation() = default;

public:
    void drawCurrentFrame() override;

protected:
    void updateSpectrogram(const Json::Value& presets, const std::string& currentPreset, const std::string& extraData) override;
    std::shared_ptr<Spectrogram> getSpectrogram() override;
    void clearSpectrogramScreen() override;
    void fillSpectrogramScreen() override;

private:
    std::weak_ptr<YandexStationFrontalController> ledDevice_;
    quasar::Screen screen_;
    std::shared_ptr<YandexStation2Spectrogram> spectrogram_;
};
