#pragma once

#include <yandex_io/libs/audio_player/base/audio_player.h>

#include <yandex_io/libs/threading/steady_condition_variable.h>

#include <atomic>
#include <thread>
#include <vector>

namespace quasar {

    class MockAudioPlayer: public AudioPlayer {
    public:
        MockAudioPlayer(const AudioPlayer::Params& params);
        ~MockAudioPlayer();

    public:
        const std::vector<AudioPlayer::Format>& supportedFormats() const override;
        bool replayAsync() override;
        bool playAsync() override;
        bool pause() override;
        bool seek(int seconds) override;
        bool isPlaying() const override;
        bool startBuffering() override;
        bool setVolume(double volume) override;
        bool stop() override;

    protected:
        int position() const override;
        int duration() const override;

    private:
        std::mutex mutex_;
        SteadyConditionVariable condVar_;
        std::thread onProgressThread_;
        std::atomic_bool notified{false};
        std::atomic_bool isPlaying_{false};
        std::atomic_bool stopped_{false};
        void sendProgressLoop();
    };

    class MockPlayerFactory: public AudioPlayerFactory {
    public:
        MockPlayerFactory();
        std::unique_ptr<AudioPlayer> createPlayer(const AudioPlayer::Params& params) override;
        std::unique_ptr<AudioPlayer::Params> createParams() override;
        std::unique_ptr<AudioPlayer::Params> params_;
    };

} // namespace quasar
