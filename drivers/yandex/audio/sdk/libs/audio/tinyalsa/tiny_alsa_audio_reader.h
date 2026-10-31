#pragma once

#include <yandex_io/libs/audio/reader/audio_reader.h>

#include <tinyalsa/asoundlib.h>

#include <string>

namespace quasar {

    class TinyAlsaAudioReader: public AudioReader {
    public:
        /**
         * @brief Link 2 pcm devices: start/stop and prepare will be called at the same time
         *        When TinyAlsaAudioReader::read will be called for the first time on any of this devices
         *        pcm_start will be called on both of them
         */
        static bool link(TinyAlsaAudioReader& device1, TinyAlsaAudioReader& device2);

    public:
        TinyAlsaAudioReader() = default;

        void open(int cardNumber, int deviceNumber, int channelCount, unsigned int rate,
                  pcm_format format, int periodSize, int periodCount);

        void close();

        /* psm_start, pcm_stop, pcm_wait calls */
        void start();
        void stop();
        /* true - there are frames to read, false timeout or error */
        bool wait(int timeoutMs);

        unsigned int getBufferSize();

        bool read(std::vector<uint8_t>& data, int frameCount) override;
        bool read(std::span<std::byte> data, int frameCount) override;
        void tryRecover() override;

        std::string getError() const override;

        int getFrameRate() const override;

        ~TinyAlsaAudioReader();

    private:
        pcm* device_ = nullptr;
        int cardNumber_ = 0;
        int deviceNumber_ = 0;
        struct pcm_config config_;
    };

} // namespace quasar
