#include "defines.h"

#include <sstream>

namespace {
    std::string typeToString(YandexIO::ChannelData::Type type) {
        using Type = YandexIO::ChannelData::Type;
        switch (type) {
            case Type::RAW:
                return "raw";
            case Type::VQE:
                return "vqe";
            case Type::FEEDBACK:
                return "feedback";
            case Type::BEAMFORMING:
                return "beamforming";
            case Type::BACKGROUND_NOISE_REDUCER:
                return "background_noise_reducer";
            case Type::MAIN_MIC_SYNC:
                return "main_mic_sync";
            case Type::AUXILIARY_MIC_SYNC:
                return "auxiliary_mic_sync";
            case Type::FEEDBACK_SYNC:
                return "feedback_sync";
        }
        return "unknown";
    }

} // namespace

namespace YandexIO {
    std::string ChannelData::toString() const {
        std::stringstream ss;
        ss << "Channel Name: " << name << " ChannelType: " << typeToString(type) << " SampleRate: " << sampleRate
           << " SampleSize: " << sampleSize << " Data Size: " << data.size() << " Is For Recognition: " << isForRecognition;
        if (meta.vqeInfo.has_value()) {
            ss << " VqeType: " << meta.vqeInfo->type << " VqePreset: " << meta.vqeInfo->preset;
        }
        if (meta.doaAngle.has_value()) {
            ss << " DOA: " << *meta.doaAngle;
        }
        return ss.str();
    }

} /* namespace YandexIO */
