#pragma once

#include <yandex_io/libs/json_utils/json_utils.h>

#include <yandex_io/modules/audio_input/audio_device/audio_device.h>

#include <yandex_io/sdk/audio_source/i_audio_source.h>

#include <string>
#include <string_view>

namespace YandexIO {

    using ChannelNameToType = std::function<YandexIO::ChannelData::Type(std::string_view)>;

    /**
     * @brief default converter. If name starts with "vqe" -> Type::VQE.
     *                           If name starts with "spk" -> Type::FEEDBACK
     *                           If name starts with "bf"  -> Type::BEAMFORMING
     *                           Otherwise -> Type::RAW
     */
    YandexIO::ChannelData::Type defaultConvertChannelNameToType(std::string_view name);

    /**
     * @brief Default converter from AudioDevice channels map to YandexIO::ChannelsData
     * @param[in] vqeType -- type of vqe. will be set up to meta to each channel of "vqe" type
     */
    YandexIO::ChannelsData convertAudioDeviceChannels(YandexIO::AudioDevice::ChannelToChunk channelToChunkMap,
                                                      const std::string& mainChannel,
                                                      const YandexIO::ChannelNameToType& typeConverter,
                                                      const YandexIO::AudioDevice::ChannelsList& channelsInfo,
                                                      const std::string& vqeType = std::string(),
                                                      const std::string& vqePreset = std::string());

    /**
     * @brief Merges two VQE json configs together, but with support of JSON VQE presets from backend.
     *        When newConfig["preset"] is an object, this function merges VQE presets together.
     *        Otherwise, this function is equal to quasar::jsonMerge.
     */
    void mergeVqeConfigsWithJsonPreset(const Json::Value& newConfig, Json::Value& defaultConfig);

} /* namespace YandexIO */
