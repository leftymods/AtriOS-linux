#include "alsa_audio_device.h"

#include <yandex_io/libs/audio/alsa/alsa_audio_reader.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <alsa/asoundlib.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <vector>

using namespace quasar;
using namespace YandexIO;

namespace {

    constexpr size_t CHANNEL_COUNT = 1;

    constexpr size_t FRAME_RATE = 16000;
    constexpr size_t FRAME_SIZE = sizeof(int16_t) * CHANNEL_COUNT;

    constexpr const char* CHANNEL_NAME = "raw_mic_0";

} // namespace

AlsaAudioDevice::AlsaAudioDevice(const Json::Value& config)
    : AudioDeviceBase({{CHANNEL_NAME, {2, FRAME_RATE}}}, config)
    , alsaReader_(std::make_unique<quasar::AlsaAudioReader>())
{
    std::string deviceName;
    if (config.isMember("deviceName")) {
        deviceName = config["deviceName"].asString();
    } else if (config.isMember("cardNumber") && config.isMember("deviceNumber")) {
        deviceName = AlsaAudioReader::buildHwDeviceName(config["cardNumber"].asInt(), config["deviceNumber"].asInt());
    } else {
        Y_VERIFY(!"there is no device information in audio config");
    }
    periodSize_ = getUInt64(config, "periodSize");

    alsaReader_->open(deviceName, CHANNEL_COUNT, FRAME_RATE, SND_PCM_FORMAT_S16_LE, periodSize_, /* periodCount = */ 1);
}

AlsaAudioDevice::~AlsaAudioDevice() {
    stop();
}

double AlsaAudioDevice::getDOAAngle() const {
    return 0.;
}

void AlsaAudioDevice::doCapture() {
    std::vector<uint8_t> audioBuf;
    ChannelToData data;
    alsaReader_->read(audioBuf, periodSize_);

    auto& dataVec = data[CHANNEL_NAME];
    dataVec.reserve(audioBuf.size() / FRAME_SIZE);

    const auto* rawAudioSamples = reinterpret_cast<const int16_t*>(audioBuf.data());

    std::copy(rawAudioSamples, rawAudioSamples + audioBuf.size() / FRAME_SIZE, std::back_inserter(dataVec));

    AudioDeviceBase::pushData(data);
}
