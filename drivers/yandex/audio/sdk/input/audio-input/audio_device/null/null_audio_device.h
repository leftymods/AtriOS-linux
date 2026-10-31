#pragma once

#include <yandex_io/modules/audio_input/audio_device/base/audio_device_base.h>

namespace YandexIO {

    /* Stub audio device that don't provide any data */
    class NullAudioDevice: public AudioDeviceBase {
    public:
        explicit NullAudioDevice(const Json::Value& config);

        double getDOAAngle() const override;

    private:
        void doCapture() override;
    };

} // namespace YandexIO
