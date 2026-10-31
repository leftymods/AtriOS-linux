#include "yandex_io_audio_source_adapter.h"

#include "audio_sender_endpoint.h"

#include <yandex_io/libs/audio/common/defines.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/json_utils/json_utils.h>

using namespace AudioSender;

namespace {

    std::unordered_set<std::string> getChannels(const Json::Value& array) {
        std::unordered_set<std::string> channels;
        for (const auto& channel : array) {
            channels.insert(channel.asString());
        }
        return channels;
    }

} // namespace

YandexIOAudioSourceAdapter::YandexIOAudioSourceAdapter(std::shared_ptr<YandexIO::IDevice> device) {
    const auto audiosenderConfig = device->configuration()->getServiceConfig(quasar::AudioSenderEndpoint::SERVICE_NAME);
    if (!audiosenderConfig.isMember("availableChannels") || !audiosenderConfig["availableChannels"].isArray()) {
        throw std::runtime_error("Can not find \"availableChannels\" in AudioSender config");
    }
    availableChannels = getChannels(audiosenderConfig["availableChannels"]);

    // Todo: handle alsa mode and other formats

    soundInfo = SpeechKit::SoundInfo(SpeechKit::SoundFormat::PCM, 1, DEFAULT_AUDIO_SAMPLING_RATE, 2);

    const auto audioConfig = device->configuration()->getServiceConfig("audiod");
    const int periodSize = quasar::tryGetInt(audioConfig, "periodSize", DEFAULT_AUDIO_PERIOD_SIZE);
    bufferCaptureTimeout = SpeechKit::SoundBuffer::calculateTimeMs(
        soundInfo, periodSize * soundInfo.getSampleSize() * soundInfo.getChannelCount());
}

void AudioSender::YandexIOAudioSourceAdapter::onAudioData(const YandexIO::ChannelsData& data) {
    std::map<std::string, SpeechKit::SoundBuffer::SharedPtr> channelToBuffers{};
    std::string mainChannel;
    for (const auto& channel : data) {
        if (channel.data.empty() || channel.name.empty()) {
            continue;
        }
        std::vector<uint8_t> audioData{};
        audioData.resize(channel.data.size() * channel.sampleSize);
        memcpy(audioData.data(), channel.data.data(), audioData.size());
        if (channel.isForRecognition) {
            // should be only one channel for recognition
            mainChannel = channel.name;
        }
        channelToBuffers[channel.name] = std::make_shared<SpeechKit::SoundBuffer>(soundInfo, std::move(audioData));
    }
    if (mainChannel.empty()) {
        // skip data without main channel. should not happen
        return;
    }

    handleAudioData(channelToBuffers, mainChannel);
}

int YandexIOAudioSourceAdapter::getBufferCaptureTimeout() const {
    return bufferCaptureTimeout;
}

const SpeechKit::SoundInfo& YandexIOAudioSourceAdapter::getSoundInfo() const {
    return soundInfo;
}

std::unordered_set<std::string> YandexIOAudioSourceAdapter::getAvailableChannelNames() const {
    return availableChannels;
}
