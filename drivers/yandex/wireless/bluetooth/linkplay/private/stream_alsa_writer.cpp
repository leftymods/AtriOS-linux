#include "stream_alsa_writer.h"

#include <yandex_io/libs/logging/logging.h>

namespace quasar {

    void StreamAlsaWriter::open(const std::string& deviceName, int channels, snd_pcm_format_t format,
                                int sampleRate, unsigned char bitsPerSample)
    {
        YIO_LOG_INFO("Open Alsa Device for Stream In. Channels: " << channels << " Sample Rate: " << sampleRate << " Format: " << format << " Bits Per Sample: " << int(bitsPerSample));
        format_ = format;
        bitsPerFrame_ = bitsPerSample * channels;
        inputSampleRate_ = sampleRate;
        channels_ = channels;
        alsaWriter_.open(deviceName, channels, sampleRate, format);
    }

    bool StreamAlsaWriter::write(std::vector<char> inputData, double volumeCoef)
    {
        /* Play input buffers */
        if (skipBuffers_) {
            return false;
        }
        auto applyVolume = [](auto data, size_t length, auto coef) {
            for (size_t i = 0; i < length; ++i) {
                data[i] = data[i] * coef;
            }
        };
        if (volumeCoef < 1.0) {
            switch (format_) {
                case SND_PCM_FORMAT_S16_LE: {
                    applyVolume((int16_t*)inputData.data(), inputData.size() / sizeof(int16_t), volumeCoef);
                    break;
                }
                case SND_PCM_FORMAT_S32_LE: {
                    applyVolume((int32_t*)inputData.data(), inputData.size() / sizeof(int32_t), volumeCoef);
                    break;
                }
                default: {
                    break;
                }
            }
        }

        /* TODO: Resample to 48KHZ to avoid Interruptions during the stream (some alsa bug Linkplay reported) */
        return alsaWriter_.write(inputData.data(), inputData.size());
    }

    void StreamAlsaWriter::skipBuffers()
    {
        skipBuffers_ = true;
    }

    void StreamAlsaWriter::playBuffers()
    {
        skipBuffers_ = false;
    }

} /* namespace quasar */
