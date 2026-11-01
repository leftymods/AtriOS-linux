#pragma once

#include "spectrogram.h"

#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/modules/leds/led_manager/ng/animation.h>
#include <yandex_io/sdk/spectrum_observer.h>

#include <deque>

class SpectrogramAnimation: public Animation {
public:
    using SpectrumFrame = YandexIO::SpectrumObserver::SpectrumFrame;

    SpectrogramAnimation(
        std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory,
        std::weak_ptr<quasar::LedDevice> ledDevice,
        std::chrono::milliseconds frameInterval = std::chrono::milliseconds(40));

    ~SpectrogramAnimation();

public:
    bool finished() const override;

    TimePoint getEndOfFrameTimePoint() const override;

    std::string getName() const override;

    void resetAnimation() override;

    void onSpectrum(const SpectrumFrame& frame);

    void setPaused(bool paused);
    bool isPaused() const;

    void updateLocalPresets(std::string configs);
    void switchLocalCurrentPreset(std::string current);
    void reloadPreset();

    void updateTime(TimePoint timePoint) override;
    void startAnimationFrom(TimePoint timePoint) override;
    std::chrono::nanoseconds getLength() const override;

protected:
    virtual void updateSpectrogram(const Json::Value& presets, const std::string& currentPreset, const std::string& extraData) = 0;
    virtual std::shared_ptr<Spectrogram> getSpectrogram() = 0;
    virtual void clearSpectrogramScreen() = 0;
    virtual void fillSpectrogramScreen() = 0;

private:
    std::vector<float> calculateBands(const SpectrumFrame& frame);
    std::optional<std::vector<float>> updateSpectrum();

    void updateExternalPresets(const std::string& configs, const std::string& current, const std::string& extraData);
    void drawSpectrogramCanvas(std::vector<float>&& bands);
    void initSpectrogram();
    void updateAndDrawCanvas();
    void sendStateUnsafe(bool force);

private:
    std::shared_ptr<quasar::ipc::IServer> server_;
    std::chrono::time_point<std::chrono::steady_clock> endOfFrame_;

    const std::chrono::milliseconds frameInterval_;

    mutable std::mutex mutex_;
    std::deque<std::pair<std::chrono::steady_clock::time_point, SpectrumFrame>> frames_;

    struct Presets {
        std::string configs;
        std::string current;
        std::string extraData;
    };
    std::optional<Presets> localPresets_;
    std::optional<Presets> externalPresets_;

    std::atomic<bool> isPaused_;
    quasar::ipc::SharedMessage lastStateMessage_;
};
