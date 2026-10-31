#pragma once

#include "yandex_music_player.h"

#include <yandex_io/interfaces/user_config/i_user_config_provider.h>
#include <yandex_io/libs/audio_player/base/audio_player.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/self_destroyer/self_destroyer.h>

#include <string>

namespace quasar {
    class MPListener: public AudioPlayer::SimpleListener {
    public:
        MPListener(std::shared_ptr<YandexIO::IDevice> device, YandexMusicPlayer* yandexMusicPlayer);

        void onError(const std::string& message) override;

        void onEnd() override;

        void onStart() override;

        void onSeeked() override;

        void onPaused() override;

        void onResumed() override;

        void onProgress(int position, int duration) override;

        void onBufferingStart() override;

        void onBufferingEnd() override;

        void onStopped() override;

        void onBufferStalled() override;

    private:
        std::shared_ptr<YandexIO::IDevice> device_;
        std::shared_ptr<const YandexIO::LatencyData> endToStartLatencyPoint_;
        std::shared_ptr<const YandexIO::LatencyData> bufferingLatencyPoint_;
        YandexMusicPlayer* yandexMusicPlayer_;
        std::shared_ptr<SelfDestroyer> selfDestroyer_;
    };

} // namespace quasar
