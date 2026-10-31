#pragma once
#include <yandex_io/libs/audio/writer/audio_writer.h>

#include <alsa/asoundlib.h>

#include <string>
#include <vector>

namespace quasar {

    /**
     * @note All methods should be called in one thread (or protected with mutex)
     */
    class AlsaAudioWriter: public AudioWriter {
    public:
        AlsaAudioWriter() = default;
        ~AlsaAudioWriter() override;

        AlsaAudioWriter& operator=(const AlsaAudioWriter&) = delete;
        AlsaAudioWriter(const AlsaAudioWriter&) = delete;

        void open(const std::string& deviceName, int numberOfChannels, unsigned int rate, snd_pcm_format_t format);
        /**
         * @brief Close alsa device and free resources
         */
        void close();
        void tryRecover() override;

        /**
         * @brief Reopen alsa with new SampleRate if passed rate is not equal to saved rate
         * @param rate -- new sampleRate
         */
        void setSampleRate(unsigned int rate);

        bool write(const std::vector<int16_t>& data) override;
        bool write(const char* data, size_t numBytes);

        size_t bufSize() const {
            return chunkSize_ * bitsPerFrame_ / 8;
        }

        std::string getError() const override;

    private:
        bool checkParamsDiffer(const std::string& deviceName, int numberOfChannels, unsigned int rate, snd_pcm_format_t format) const;

        std::string deviceName_;

        size_t chunkSize_{};
        int bitsPerSample_{};
        int bitsPerFrame_{};
        snd_pcm_uframes_t bufferSize_{};

        snd_pcm_t* device_ = nullptr;
        snd_pcm_hw_params_t* hwParams_ = nullptr;
        snd_pcm_sw_params_t* swParams_ = nullptr;
        int err_ = 0;
        int hwChannels_ = 0;
        snd_pcm_format_t format_ = SND_PCM_FORMAT_S16_LE;
        unsigned int rate_ = 48000;
    };

} // namespace quasar
