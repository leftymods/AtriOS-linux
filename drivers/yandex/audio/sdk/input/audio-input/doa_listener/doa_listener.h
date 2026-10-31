#pragma once

#include <yandex_io/sdk/audio_source/i_audio_source_client.h>

#include <functional>

namespace YandexIO {
    /**
     * @brief Simple listener that takes Main channel (used for recognition) and call OnDoaChanged_ callback if changed
     */
    class DoaListener: public IAudioSourceClient::Listener {
    public:
        using OnDoaChanged = std::function<void(double doa)>;
        explicit DoaListener(OnDoaChanged onDoaChanged);

        void onAudioData(const ChannelsData& data) override;

    private:
        OnDoaChanged onDoaChanged_;
        double doa_{45.0};
    };

} /* namespace YandexIO */
