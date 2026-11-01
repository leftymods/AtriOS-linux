#pragma once

#include <yandex_io/libs/audio_player/base/audio_player.h>

#include <yandex_io/libs/audio/alsa/alsa_audio_writer.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace quasar {

    class AlsaAudioPlayer: public AudioPlayer {
    public:
        AlsaAudioPlayer(const AudioPlayer::Params& params);

        AlsaAudioPlayer(const AlsaAudioPlayer&) = delete;
        AlsaAudioPlayer& operator=(const AlsaAudioPlayer&) = delete;

        ~AlsaAudioPlayer() override = default;

    public:
        const std::vector<AudioPlayer::Format>& supportedFormats() const override;

        bool playAsync() override;
        bool pause() override;
        bool seek(int /* seconds */) override {
            return false;
        }
        bool isPlaying() const override {
            return curTrack_.isPlaying();
        }
        bool startBuffering() override {
            return false;
        };
        bool setVolume(double /* volume */) override {
            return false;
        };

    protected:
        int position() const override {
            return -1;
        }
        int duration() const override {
            return -1;
        }

    private:
        class Track final {
        public:
            Track(AlsaAudioPlayer* player);
            ~Track();

            void lock() {
                playbackThreadLock_.lock();
            }
            void unlock() {
                playbackThreadLock_.unlock();
            }

            void open(const std::string& deviceName, const std::string& path);
            void close();

            void startUnlock();
            void stop();

            bool isPlaying() const {
                return !stopped_;
            }

        private:
            AlsaAudioPlayer* player_;

            void playbackThread();

            enum class PlaybackState {
                PLAY_SCHEDULED,
                PLAY_ACTIVE,
                PLAY_FINISHED,
            };

            std::thread playbackThread_;
            bool playbackThreadStopped_{false};

            PlaybackState playbackState_{PlaybackState::PLAY_ACTIVE};

            std::unique_ptr<AlsaAudioWriter> audioWriter_;
            FILE* file_{};

            bool isOpen_{false};
            std::atomic<bool> stopped_{true};

            std::mutex playbackThreadLock_;
            quasar::SteadyConditionVariable playbackThreadCondVar_;

            std::vector<char> audioBuf_;
        };

        Track curTrack_;
    };

} // namespace quasar
