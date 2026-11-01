#pragma once

#include <yandex_io/libs/errno/errno_exception.h>

#include <alsa/asoundlib.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace quasar {

    struct WavDecoder final {
        struct Result {
            int channelsNum{};
            int rate{};
            snd_pcm_format_t fmt{SND_PCM_FORMAT_UNKNOWN};
            long dataPos{};
        };

        static Result decode(FILE* wavFile) {
            Result res;

            wav_header header;
            if (fread(&header, sizeof(struct wav_header), 1, wavFile) != 1) {
                throw std::runtime_error(std::string("Error reading WAV header: ") + strError(errno));
            }

            if ((header.riff_id != ID_RIFF) ||
                (header.riff_fmt != ID_WAVE) ||
                (header.fmt_id != ID_FMT) ||
                !((header.audio_format == FORMAT_PCM) || (header.audio_format == FORMAT_FLOAT))) {
                throw std::runtime_error("Error: file is not a PCM riff/wave file");
            }

            res.channelsNum = header.num_channels;
            res.rate = header.sample_rate;

            if (header.audio_format == FORMAT_PCM) {
                if (header.bits_per_sample == 16) {
                    res.fmt = SND_PCM_FORMAT_S16_LE;
                }

                /* Refer to https://github.com/bear24rw/alsa-utils/blob/master/aplay/aplay.c#L927 */
                if (header.bits_per_sample == 24) {
                    switch (header.block_align / header.num_channels) {
                        case 3:
                            res.fmt = SND_PCM_FORMAT_S24_3LE;
                            break;

                        case 4:
                            res.fmt = SND_PCM_FORMAT_S24_LE;
                            break;
                    }
                }

                if (header.bits_per_sample == 32) {
                    res.fmt = SND_PCM_FORMAT_S32_LE;
                }
            }

            if (header.audio_format == FORMAT_FLOAT) {
                if (header.bits_per_sample == 32) {
                    res.fmt = SND_PCM_FORMAT_FLOAT_LE;
                }
            }

            if (res.fmt == SND_PCM_FORMAT_UNKNOWN) {
                std::cerr << "format = " << header.audio_format << " bits = " << header.bits_per_sample << "\n";
                throw std::runtime_error("Error: unsupported WAV format ");
            }

            res.dataPos = ftell(wavFile);
            if (res.dataPos < 0) {
                throw std::runtime_error("Error: getting audio data position in file");
            }

            return res;
        }

        struct wav_header {
            uint32_t riff_id;
            uint32_t riff_sz;
            uint32_t riff_fmt;
            uint32_t fmt_id;
            uint32_t fmt_sz;
            uint16_t audio_format;
            uint16_t num_channels;
            uint32_t sample_rate;
            uint32_t byte_rate;
            uint16_t block_align;
            uint16_t bits_per_sample;
            uint32_t data_id;
            uint32_t data_sz;
        };

        constexpr static const uint32_t ID_RIFF = 0x46464952;
        constexpr static const uint32_t ID_WAVE = 0x45564157;
        constexpr static const uint32_t ID_FMT = 0x20746d66;
        constexpr static const uint32_t ID_DATA = 0x61746164;

        constexpr static const uint32_t FORMAT_PCM = 0x0001;
        constexpr static const uint32_t FORMAT_FLOAT = 0x0003;
    };

} // namespace quasar
