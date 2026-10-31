#pragma once

#include <yandex_io/libs/audio/common/defines.h>
#include <yandex_io/libs/audio/reader/audio_reader.h>

#include <alsa/asoundlib.h>

#include <string>

namespace quasar {

    class AlsaAudioReader: public AudioReader {
    public:
        AlsaAudioReader();
        ~AlsaAudioReader() override;

        void open(const std::string& deviceName, int channelCount, unsigned int rate, snd_pcm_format_t format, int periodSize, int periodCount);
        bool read(std::vector<uint8_t>& data, int framesCount) override;
        bool read(std::span<std::byte> data, int framesCount) override;

        void tryRecover() override;
        std::string getError() const override;
        int getFrameRate() const override;

        // Build ALSA hw device name, e.g. "hw:1,0"
        static std::string buildHwDeviceName(int cardNumber, int deviceNumber);

    private:
        void logStatus();
        int frameSize() const {
            return hwChannels_ * HWsampleSize_;
        }

        snd_pcm_t* device_ = nullptr;
        snd_pcm_hw_params_t* hwParams_ = nullptr;
        snd_pcm_status_t* status_ = nullptr;
        int err_ = 0;
        std::string deviceName_;
        int hwChannels_ = 0;
        snd_pcm_format_t format_ = SND_PCM_FORMAT_S32_LE;
        snd_pcm_uframes_t periodSize_ = DEFAULT_AUDIO_PERIOD_SIZE;
        unsigned int rate_ = DEFAULT_AUDIO_SAMPLING_RATE;
        int HWsampleSize_ = 0;
    };

} // namespace quasar
