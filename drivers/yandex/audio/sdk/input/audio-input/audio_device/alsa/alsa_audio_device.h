#pragma once

#include <yandex_io/modules/audio_input/audio_device/base/audio_device_base.h>

#include <json/json.h>

#include <memory>

namespace quasar {
    class AlsaAudioReader;
} // namespace quasar

namespace YandexIO {

    // Device to capture 1 channel by alsa
    class AlsaAudioDevice: public AudioDeviceBase {
    public:
        explicit AlsaAudioDevice(const Json::Value& config);

        ~AlsaAudioDevice();
        double getDOAAngle() const override;

    private:
        void doCapture() override;

    private:
        size_t periodSize_;
        std::unique_ptr<quasar::AlsaAudioReader> alsaReader_;
    };

} // namespace YandexIO
