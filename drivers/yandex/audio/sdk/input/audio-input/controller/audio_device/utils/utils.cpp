#include "utils.h"

namespace YandexIO {

    ChannelData::Type defaultConvertChannelNameToType(std::string_view name) {
        ChannelData::Type type = ChannelData::Type::RAW;
        if (name.find("vqe_bnr") == 0) {
            type = ChannelData::Type::BACKGROUND_NOISE_REDUCER;
        } else if (name.find("vqe_bf") == 0) {
            type = ChannelData::Type::BEAMFORMING;
        } else if (name.find("vqe") == 0) {
            type = ChannelData::Type::VQE;
        } else if (name.find("raw_spk") == 0 || name.find("lb") == 0) {
            type = ChannelData::Type::FEEDBACK;
        } else if (name.find("raw_sync_mic_main") == 0) {
            type = ChannelData::Type::MAIN_MIC_SYNC;
        } else if (name.find("raw_sync_mic_aux") == 0) {
            type = ChannelData::Type::AUXILIARY_MIC_SYNC;
        } else if (name.find("raw_sync_spk") == 0) {
            type = ChannelData::Type::FEEDBACK_SYNC;
        }

        return type;
    }

    YandexIO::ChannelsData convertAudioDeviceChannels(YandexIO::AudioDevice::ChannelToChunk channelToChunkMap,
                                                      const std::string& mainChannel,
                                                      const YandexIO::ChannelNameToType& typeConverter,
                                                      const YandexIO::AudioDevice::ChannelsList& channelsInfo,
                                                      const std::string& vqeType,
                                                      const std::string& vqePreset) {
        auto getSampleRate = [&channelsInfo](const std::string& channelName) {
            auto iter = channelsInfo.find(channelName);
            return iter == channelsInfo.end() ? 16000 : iter->second.sampleRate;
        };

        YandexIO::ChannelsData result;
        for (auto& [name, chunk] : channelToChunkMap) {
            YandexIO::ChannelData channel;
            channel.name = name;
            channel.type = typeConverter(name);
            if (channel.type == ChannelData::Type::VQE && !vqeType.empty()) {
                channel.meta.vqeInfo = ChannelData::VqeInfo{vqeType, vqePreset};
            }
            channel.data = std::move(chunk.data);
            switch (channel.type) {
                case ChannelData::Type::VQE:
                    // only processed channels has doaAngle
                    channel.meta.doaAngle = chunk.doaAngle;
                    break;
                default:
                    break;
            }
            channel.isForRecognition = (channel.name == mainChannel);
            channel.sampleRate = getSampleRate(name);

            if (channel.name.starts_with("raw_il_")) {
                channel.local = true;
            }

            result.push_back(std::move(channel));
        }

        return result;
    }

    void mergeVqeConfigsWithJsonPreset(const Json::Value& newConfig, Json::Value& defaultConfig) {
        if (!newConfig.isObject() || !defaultConfig.isObject()) {
            return;
        }
        std::string newPresetStr = quasar::tryGetString(defaultConfig, "preset", "");
        quasar::jsonMerge(newConfig, defaultConfig);
        if (const auto& jsonPreset = newConfig["preset"]; jsonPreset.isObject()) {
            newPresetStr += '@';

            Json::FastWriter jsonWriter;
            jsonWriter.omitEndingLineFeed();
            newPresetStr += jsonWriter.write(jsonPreset);

            defaultConfig["preset"] = newPresetStr;
        }
    }

} /* namespace YandexIO */
