#include "yandex_vqe_audio_device_base.h"

#include <yandex_io/libs/base/utils.h>

#include <cstdint>

using namespace YandexIO;

namespace {
    using DEFAULT_CHANNEL_SAMPLE_TYPE = std::int16_t;
    constexpr unsigned DEFAULT_CHANNEL_SAMPLE_WIDTH = (unsigned)sizeof(DEFAULT_CHANNEL_SAMPLE_TYPE);
    constexpr float DEFAULT_FP_TO_INT_SCALE = (float)(1 << (8 * DEFAULT_CHANNEL_SAMPLE_WIDTH - 1));

    struct YandexVqeOutputChannelTypeInfo {
        const char* name;
        ChannelData::Type type;

        std::string getChannelName(const std::string& mainChannelName, size_t n) const {
            std::string result{name};
            if (result.empty()) {
                result = mainChannelName;
            }
            if (!result.empty() && result.back() == '_') {
                result.append(std::to_string(n));
            } else {
                Y_VERIFY(n == 0);
            }
            return result;
        }
    };

    constexpr YandexVqeOutputChannelTypeInfo OUTPUT_CHANNELS_TYPES_INFO[] = {
        {
            .name = "", // This value will be automatically replaced by vqe channel name for specific platform (e.g, "vqe_0")
            .type = ChannelData::Type::VQE,
        },
        {
            .name = "vqe_bf_",
            .type = ChannelData::Type::BEAMFORMING,
        },
        {
            .name = "vqe_bnr",
            .type = ChannelData::Type::BACKGROUND_NOISE_REDUCER,
        },
        {
            .name = "raw_sync_mic_main",
            .type = ChannelData::Type::MAIN_MIC_SYNC,
        },
        {
            .name = "raw_sync_mic_aux_",
            .type = ChannelData::Type::AUXILIARY_MIC_SYNC,
        },
        {
            .name = "raw_sync_spk_",
            .type = ChannelData::Type::FEEDBACK_SYNC,
        },
    };

    std::map<ChannelData::Type, size_t>
    makeExpectedChannelsByType(const YandexVqeAudioDeviceBase::ChannelsCounters& channelsCounters, int beamformingDirections) {
        if (!channelsCounters.hasVqe) {
            return {};
        }

        return {
            {ChannelData::Type::VQE, 1u},
            {ChannelData::Type::BACKGROUND_NOISE_REDUCER, 1u},
            {ChannelData::Type::BEAMFORMING, (size_t)std::max(beamformingDirections, 0)},
            {ChannelData::Type::MAIN_MIC_SYNC, 1u},
            {ChannelData::Type::AUXILIARY_MIC_SYNC, (size_t)std::max(channelsCounters.mics - 1, 0)},
            {ChannelData::Type::FEEDBACK_SYNC, (size_t)channelsCounters.spks},
        };
    }

    AudioDevice::ChannelsList& makeAvailableChannelsList(AudioDevice::ChannelsList& availableChannels,
                                                         const std::map<ChannelData::Type, size_t>& expectedChannelsByType,
                                                         const std::string& vqeMainChannelName)
    {
        AudioDevice::ChannelInfo channelInfo{DEFAULT_CHANNEL_SAMPLE_WIDTH, (unsigned)DEFAULT_AUDIO_SAMPLING_RATE};
        for (const auto& channelTypeInfo : OUTPUT_CHANNELS_TYPES_INFO) {
            const size_t expectedChannels = expectedChannelsByType.at(channelTypeInfo.type);
            for (size_t i = 0; i < expectedChannels; ++i) {
                availableChannels.emplace(channelTypeInfo.getChannelName(vqeMainChannelName, i), channelInfo);
            }
        }
        return availableChannels;
    }
} // namespace

YandexVqeAudioDeviceBase::YandexVqeAudioDeviceBase(const ChannelsCounters& channelsCounters,
                                                   const std::string& vqeMainChannelName,
                                                   int beamformingDirections,
                                                   AudioDevice::ChannelsList availableChannels,
                                                   std::shared_ptr<YandexIO::VqeController> vqeController,
                                                   const Json::Value& config)
    : YandexVqeAudioDeviceBase(
          makeExpectedChannelsByType(channelsCounters, beamformingDirections),
          vqeMainChannelName,
          std::move(availableChannels),
          std::move(vqeController),
          config)
{
}

YandexVqeAudioDeviceBase::YandexVqeAudioDeviceBase(std::map<ChannelData::Type, size_t> expectedChannelsByType,
                                                   const std::string& vqeMainChannelName,
                                                   AudioDevice::ChannelsList availableChannels,
                                                   std::shared_ptr<YandexIO::VqeController> vqeController,
                                                   const Json::Value& config)
    : AudioDeviceBase(
          std::move(makeAvailableChannelsList(availableChannels, expectedChannelsByType, vqeMainChannelName)),
          config)
    , vqeController_(std::move(vqeController))
    , vqeMainChannelName_(vqeMainChannelName)
    , expectedChannelsByType_(std::move(expectedChannelsByType))
{
}

void YandexVqeAudioDeviceBase::captureVqeChannels(ChannelToData& channelsData) {
    auto vqeEngine = vqeController_->getEngine();
    for (const auto& channelTypeInfo : OUTPUT_CHANNELS_TYPES_INFO) {
        const size_t expectedChannels = expectedChannelsByType_.at(channelTypeInfo.type);
        const size_t capturedChannels = std::min(vqeEngine->getOutputChannelCount(channelTypeInfo.type), expectedChannels);
        for (size_t i = 0; i < expectedChannels; ++i) {
            auto channelName = channelTypeInfo.getChannelName(vqeMainChannelName_, i);
            if (i < capturedChannels && isChannelCaptured(channelName)) {
                auto vqeDataBuffer = vqeEngine->getOutputChannelData(channelTypeInfo.type, i);
                quasar::convertFPSamplesToInt(channelsData[channelName], vqeDataBuffer, 1, DEFAULT_FP_TO_INT_SCALE);
            } else {
                channelsData.erase(channelName);
            }
        }
    }
}

unsigned YandexVqeAudioDeviceBase::getAbsentChannelCount() const {
    auto vqeEngine = vqeController_->getEngine();
    unsigned result = 0u;
    for (const auto& channelTypeInfo : OUTPUT_CHANNELS_TYPES_INFO) {
        const size_t expectedChannels = expectedChannelsByType_.at(channelTypeInfo.type);
        const size_t capturedChannels = std::min(vqeEngine->getOutputChannelCount(channelTypeInfo.type), expectedChannels);
        result += expectedChannels - capturedChannels;
    }
    return result;
}
