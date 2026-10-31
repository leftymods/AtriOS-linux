#include "channels_info_dumper.h"

#include <yandex_io/libs/logging/logging.h>

namespace YandexIO {

    void ChannelsInfoDumper::onAudioData(const ChannelsData& data) {
        YIO_LOG_INFO("...onAudioData...");
        for (const auto& channel : data) {
            YIO_LOG_INFO(channel.toString());
        }
    }

} /* namespace YandexIO */
