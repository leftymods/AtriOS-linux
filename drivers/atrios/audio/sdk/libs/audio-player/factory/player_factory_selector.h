#pragma once

#include <yandex_io/libs/audio_player/base/audio_player.h>

#include <atomic>

namespace quasar {
    class PlayerFactorySelector: public AudioPlayerFactory {
    public:
        explicit PlayerFactorySelector(const Json::Value& config);

        std::unique_ptr<AudioPlayer> createPlayer(const AudioPlayer::Params& params) override;
        std::unique_ptr<AudioPlayer::Params> createParams() override;

        void configUpdated(const Json::Value& config) override;

    private:
        std::unique_ptr<AudioPlayerFactory> playerFactory_;
    };
} // namespace quasar
