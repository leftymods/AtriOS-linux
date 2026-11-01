#pragma once

#include <yandex_io/sdk/audio_source/defines.h>

#include <yandex_io/protos/yandex_io.pb.h>

namespace quasar {
    quasar::proto::IOAudioChannel convert(const YandexIO::ChannelData& channel);

    YandexIO::ChannelData convert(const quasar::proto::IOAudioChannel& protoChannel);

    YandexIO::ChannelsData convert(const quasar::proto::IOAudioData& data);

} // namespace quasar
