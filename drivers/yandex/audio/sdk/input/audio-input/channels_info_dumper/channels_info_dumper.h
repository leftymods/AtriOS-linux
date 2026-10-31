#pragma once

#include <yandex_io/sdk/audio_source/i_audio_source_client.h>

namespace YandexIO {
    /**
     * @brief Simple IAudioSourceClient listener that print channels info to "info" logs
     */
    class ChannelsInfoDumper: public IAudioSourceClient::Listener {
    public:
        ChannelsInfoDumper() = default;

        void onAudioData(const ChannelsData& data) override;
    };

} /* namespace YandexIO */
