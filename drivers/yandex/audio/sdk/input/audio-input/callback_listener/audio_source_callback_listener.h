#pragma once

#include <yandex_io/sdk/audio_source/i_audio_source_client.h>

#include <functional>

namespace YandexIO {
    /**
     * @brief Simple IAudioSourceClient listener that forward ChannelsData to callback
     */
    class AudioSourceCallbackListener: public IAudioSourceClient::Listener {
    public:
        using OnAudioData = std::function<void(const ChannelsData&)>;
        explicit AudioSourceCallbackListener(OnAudioData onAudioDataCb);

        void onAudioData(const ChannelsData& data) override;

    private:
        OnAudioData onAudioData_;
    };

} /* namespace YandexIO */
