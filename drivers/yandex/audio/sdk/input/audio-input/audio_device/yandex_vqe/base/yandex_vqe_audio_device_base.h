#pragma once

#include <yandex_io/modules/audio_input/audio_device/base/audio_device_base.h>

#include <yandex_io/modules/audio_input/vqe/controller/vqe_controller.h>

#include <yandex_io/libs/audio/common/defines.h>
#include <yandex_io/sdk/audio_source/defines.h>

namespace YandexIO {
    class YandexVqeAudioDeviceBase: public AudioDeviceBase {
    public:
        struct ChannelsCounters {
            int mics;
            int spks;
            int micsUnknown;
            int total;
            bool hasVqe;
        };

        YandexVqeAudioDeviceBase(const ChannelsCounters& /*channelsCounters*/,
                                 const std::string& vqeMainChannelName,
                                 int beamformingDirections,
                                 AudioDevice::ChannelsList /*availableChannels*/,
                                 std::shared_ptr<YandexIO::VqeController> vqeController,
                                 const Json::Value& config);

        unsigned getAbsentChannelCount() const override;

        inline std::weak_ptr<YandexIO::VqeController> getVqeController() {
            return vqeController_;
        }

    protected:
        void captureVqeChannels(ChannelToData& /*channelsData*/);

        std::shared_ptr<YandexIO::VqeController> vqeController_;

    private:
        YandexVqeAudioDeviceBase(std::map<ChannelData::Type, size_t> expectedChannelsByType,
                                 const std::string& vqeMainChannelName,
                                 AudioDevice::ChannelsList /*availableChannels*/,
                                 std::shared_ptr<YandexIO::VqeController> vqeController,
                                 const Json::Value& config);

        std::string vqeMainChannelName_;
        std::map<ChannelData::Type, size_t> expectedChannelsByType_;
    };
} // namespace YandexIO
