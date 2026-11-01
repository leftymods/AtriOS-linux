#pragma once

#include <yandex_io/libs/audio/writer/audio_writer.h>

#include <tinyalsa/asoundlib.h>

#include <string>
#include <vector>

namespace quasar {

    class TinyAlsaAudioWriter: public AudioWriter {
    public:
        TinyAlsaAudioWriter(){};

        void open(int cardNumber, int deviceNumber, int numberOfChannels, unsigned int rate, pcm_format format, int periodSize, int periodCount);

        void tryRecover() override;

        bool write(const std::vector<int16_t>& data) override;

        std::string getError() const override;

        ~TinyAlsaAudioWriter() override;

    private:
        TinyAlsaAudioWriter& operator=(const TinyAlsaAudioWriter&) = delete;
        TinyAlsaAudioWriter(const TinyAlsaAudioWriter&) = delete;

        pcm* device_ = nullptr;
        struct pcm_config config_;
        int cardNumber_ = 0;
        int deviceNumber_ = 0;
    };

} // namespace quasar
