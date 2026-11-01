#pragma once

#include "audio_source_adapter.h"

#include <yandex_io/libs/device/device.h>
#include <yandex_io/sdk/audio_source/i_audio_source_client.h>

namespace AudioSender {

    /**
     * @brief AudioSourceAdapter that receive AudioData from YandexIO::IAudioSource
     * @note Information about availableChannels should be contained in ["audiosender"]["availableChannels"] section
     *       of quasar.cfg
     */
    class YandexIOAudioSourceAdapter: public AudioSourceAdapter, public YandexIO::IAudioSourceClient::Listener {
    public:
        explicit YandexIOAudioSourceAdapter(std::shared_ptr<YandexIO::IDevice> device);
        std::unordered_set<std::string> getAvailableChannelNames() const override;

    public:
        /* from SpeechKit::AudioSource */
        int getBufferCaptureTimeout() const override;
        const SpeechKit::SoundInfo& getSoundInfo() const override;

    public:
        /* IAudioSourceClient listener */
        void onAudioData(const YandexIO::ChannelsData& data) override;

    private:
        SpeechKit::SoundInfo soundInfo;
        int bufferCaptureTimeout;
        std::unordered_set<std::string> availableChannels;
    };

} /* namespace AudioSender */
