#include "audio_source_utils.h"

namespace {
    namespace proto = quasar::proto;

    proto::IOAudioChannel::Type convertType(YandexIO::ChannelData::Type type) {
        static_assert(proto::IOAudioChannel::Type_ARRAYSIZE == 8, "There is new AudioChannel type. check convert");
        static_assert(static_cast<int>(YandexIO::ChannelData::Type::RAW) == proto::IOAudioChannel::RAW);
        static_assert(static_cast<int>(YandexIO::ChannelData::Type::VQE) == proto::IOAudioChannel::VQE);
        static_assert(static_cast<int>(YandexIO::ChannelData::Type::FEEDBACK) == proto::IOAudioChannel::FEEDBACK);
        static_assert(static_cast<int>(YandexIO::ChannelData::Type::BEAMFORMING) == proto::IOAudioChannel::BEAMFORMING);
        static_assert(static_cast<int>(YandexIO::ChannelData::Type::BACKGROUND_NOISE_REDUCER) == proto::IOAudioChannel::BACKGROUND_NOISE_REDUCER);
        static_assert(static_cast<int>(YandexIO::ChannelData::Type::MAIN_MIC_SYNC) == proto::IOAudioChannel::MAIN_MIC_SYNC);
        static_assert(static_cast<int>(YandexIO::ChannelData::Type::AUXILIARY_MIC_SYNC) == proto::IOAudioChannel::AUXILIARY_MIC_SYNC);
        static_assert(static_cast<int>(YandexIO::ChannelData::Type::FEEDBACK_SYNC) == proto::IOAudioChannel::FEEDBACK_SYNC);

        return static_cast<proto::IOAudioChannel::Type>(type);
    }

    YandexIO::ChannelData::Type convertType(proto::IOAudioChannel::Type type) {
        return static_cast<YandexIO::ChannelData::Type>(type);
    }

} // namespace

namespace quasar {
    proto::IOAudioChannel convert(const YandexIO::ChannelData& channel) {
        proto::IOAudioChannel protoChannel;
        protoChannel.set_name(TString(channel.name));
        protoChannel.set_type(convertType(channel.type));
        protoChannel.set_is_for_recognition(channel.isForRecognition);
        protoChannel.set_sample_rate(channel.sampleRate);
        if (channel.meta.vqeInfo.has_value()) {
            protoChannel.mutable_meta()->mutable_vqe_info()->set_type(TString(channel.meta.vqeInfo->type));
            protoChannel.mutable_meta()->mutable_vqe_info()->set_preset(TString(channel.meta.vqeInfo->preset));
        }

        if (channel.meta.doaAngle.has_value()) {
            protoChannel.mutable_meta()->set_doa_angle(*channel.meta.doaAngle);
        }

        const auto dataSize = channel.data.size() * sizeof(std::decay_t<decltype(channel.data)>::value_type);
        protoChannel.mutable_data()->assign((char*)channel.data.data(), dataSize);
        return protoChannel;
    }

    YandexIO::ChannelData convert(const proto::IOAudioChannel& protoChannel) {
        YandexIO::ChannelData channel;
        channel.name = protoChannel.name();
        channel.type = convertType(protoChannel.type());
        channel.isForRecognition = protoChannel.is_for_recognition();
        channel.sampleRate = protoChannel.sample_rate();
        const auto& protoAudioData = protoChannel.data();
        const auto dataSize = protoAudioData.size() / channel.sampleSize;
        channel.data.resize(dataSize);
        memcpy(channel.data.data(), protoAudioData.data(), protoAudioData.size());

        if (protoChannel.has_meta()) {
            if (protoChannel.meta().has_vqe_info()) {
                const auto& protoVqeInfo = protoChannel.meta().vqe_info();
                channel.meta.vqeInfo = YandexIO::ChannelData::VqeInfo{protoVqeInfo.type(), protoVqeInfo.preset()};
            }
            if (protoChannel.meta().has_doa_angle()) {
                channel.meta.doaAngle = protoChannel.meta().doa_angle();
            }
        }
        return channel;
    }

    YandexIO::ChannelsData convert(const quasar::proto::IOAudioData& data) {
        YandexIO::ChannelsData result;
        for (const auto& channel : data.channels()) {
            result.push_back(convert(channel));
        }
        return result;
    }

} // namespace quasar
