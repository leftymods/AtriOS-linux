#pragma once

#include <yandex_io/libs/audio/alsa/alsa_audio_writer.h>

#include <atomic>
#include <string>

namespace quasar {

    /**
 * TODO: Add resampling to 48KHZ to avoid stream interrupts
 */
    class StreamAlsaWriter final {
    public:
        StreamAlsaWriter() = default;

        /**
     * @brief Open alsa device to write data
     * @param deviceName - Alsa Device name
     * @param channels - Number of channels
     * @param format - Format (s32, s16, ...)
     * @param sampleRate - pcm data samplerate
     * @param bitsPerSample - Bits per sample
     */
        void open(const std::string& deviceName, int channels, snd_pcm_format_t format, int sampleRate, unsigned char bitsPerSample);

        /**
     * @brief Write data in alsa device (if skipBuffers was called then data will be dropped)
     * @param data - Input stream
     * @param volumeCoef - Volume coef. when volumeCoef < 1.0 -> it will be applied to each data sample (sample * volumeCoef)
     * @return - true - wrote successfully, false if error happened or skipBuffers was called previously
     */
        bool write(std::vector<char> data, double volumeCoef);

        /**
     * @brief Set up flag to skip any buffers that will be passed to `write` until `playBuffers` will be called
     */
        void skipBuffers();
        /**
     * @brief Allow StreamAlsaWriter to write data into Alsa in write function
     */
        void playBuffers();

    private:
        snd_pcm_format_t format_{SND_PCM_FORMAT_UNKNOWN};
        int channels_{-1};
        int inputSampleRate_{-1};
        uint8_t bitsPerFrame_{0};
        std::atomic_bool skipBuffers_{true};
        AlsaAudioWriter alsaWriter_;
    };

} /* namespace quasar */
