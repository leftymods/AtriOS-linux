#pragma once

#include <cstdint>
#include <list>
#include <optional>
#include <string>
#include <vector>

namespace YandexIO {
    struct ChannelData {
    public:
        enum class Type {
            RAW = 0,
            VQE = 1,
            FEEDBACK = 2,
            BEAMFORMING = 3,
            BACKGROUND_NOISE_REDUCER = 4,
            MAIN_MIC_SYNC = 5,
            AUXILIARY_MIC_SYNC = 6,
            FEEDBACK_SYNC = 7,
        };
        std::string name;
        Type type;
        int sampleRate = 16000;
        /* YandexIO works with S16_LE */
        using SampleInt = std::int16_t;
        const int sampleSize = sizeof(SampleInt);
        std::vector<SampleInt> data;
        bool isForRecognition; /* Should be used for Spotter and ASR */

        struct VqeInfo {
            std::string type;
            std::string preset;

            bool operator==(const VqeInfo& other) const {
                return type == other.type && preset == other.preset;
            }
        };

        struct Meta {
            std::optional<VqeInfo> vqeInfo; /* Should be set up for "VQE" type channel only */
            std::optional<double> doaAngle;
        };
        Meta meta;

        bool local = false; /* is it channel for local clients only */

    public:
        /**
         * @brief Simple helper that convert ChannelData to info string
         * @note: do not convert actual data to string, only meta info: type, name, etc
         */
        std::string toString() const;
    };

    using ChannelsData = std::list<ChannelData>;

    enum class RequestChannelType {
        MAIN, /* will receive only main channel (for recognition) */
        VQE,  /* will receive only 1 vqe channel */
        RAW,  /* will receive only 1 raw channel */
        ALL   /* will receive all channels AudioSource provide */
    };

} /* namespace YandexIO */
