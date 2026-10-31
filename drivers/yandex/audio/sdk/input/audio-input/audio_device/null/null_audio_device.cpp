#include "null_audio_device.h"

#include <chrono>
#include <thread>

namespace YandexIO {

    NullAudioDevice::NullAudioDevice(const Json::Value& config)
        : AudioDeviceBase({}, config)
    {
    }

    double NullAudioDevice::getDOAAngle() const {
        return 0.45f;
    }

    void NullAudioDevice::doCapture()
    {
        /* simply yield */
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

} // namespace YandexIO
