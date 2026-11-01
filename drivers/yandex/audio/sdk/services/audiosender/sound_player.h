#pragma once

#include <speechkit/AudioPlayer.h>
#include <speechkit/SoundInfo.h>

#include <functional>
#include <list>
#include <memory>
#include <vector>

namespace AudioSender {

    class SoundPlayer: public SpeechKit::AudioPlayer::AudioPlayerListener,
                       public std::enable_shared_from_this<SoundPlayer> {
    public:
        SoundPlayer(SpeechKit::AudioPlayer::SharedPtr audioPlayer);

        using SharedPtr = std::shared_ptr<SoundPlayer>;

        void start(std::list<SpeechKit::SoundBuffer::SharedPtr> buffers, std::function<void(bool)> endCallback);
        void stop();

    private:
        void onPlayingBegin(SpeechKit::AudioPlayer::SharedPtr /* player */) override {
        }
        void onPlayingPaused(SpeechKit::AudioPlayer::SharedPtr /* player */) override {
        }
        void onPlayingResumed(SpeechKit::AudioPlayer::SharedPtr /* player */) override {
        }
        void onPlayingDone(SpeechKit::AudioPlayer::SharedPtr player) override;
        void onPlayingData(SpeechKit::AudioPlayer::SharedPtr /* player */, SpeechKit::SoundBuffer::SharedPtr /* soundBuffer */) override {
        }
        void onPlayingError(SpeechKit::AudioPlayer::SharedPtr player, const SpeechKit::Error& error) override;
        void onBufferUnderrun(SpeechKit::AudioPlayer::SharedPtr /* player */) override {
        }

    private:
        std::function<void(bool)> endCallback;
        using AudioPlayer = SpeechKit::AudioPlayer::SharedPtr;
        AudioPlayer audioPlayer;
    };

} // namespace AudioSender
