#pragma once

#include "gstreamer.h"

#include <yandex_io/libs/audio_player/base/audio_player.h>

#include <yandex_io/libs/threading/steady_condition_variable.h>

#include <gst/gst.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <chrono>

namespace quasar {
    namespace gstreamer {

        struct GMainContextDeleter {
            void operator()(GMainContext* ptr) {
                g_main_context_unref(ptr);
            }
        };
        struct GMainLoopDeleter {
            void operator()(GMainLoop* ptr) {
                g_main_loop_unref(ptr);
            }
        };
        struct GstElementDeleter {
            void operator()(GstElement* ptr) {
                g_object_unref(ptr);
            }
        };

        class GstreamerAudioPlayer: public AudioPlayer {
            friend class GstreamerAudioPlayerFactory;

        public:
            GstreamerAudioPlayer(GstreamerAudioPlayer const&) = delete;
            GstreamerAudioPlayer& operator=(GstreamerAudioPlayer const&) = delete;

            ~GstreamerAudioPlayer();

        public:
            bool playAsync() override;
            bool replayAsync() override;
            bool pause() override;
            bool seek(int ms) override;
            bool stop() override;
            bool playMultiroom(std::chrono::nanoseconds basetime, std::chrono::nanoseconds position) override;
            const std::vector<AudioPlayer::Format>& supportedFormats() const override;
            bool isPlaying() const override {
                return playing_;
            }
            bool startBuffering() override;
            bool setVolume(double volume) override;
            Channel channel() override;
            bool setChannel(Channel ch) override;

            bool setEqualizerConfig(const YandexIO::EqualizerConfig& config) override;

            virtual void init(const std::string& pipeline);

            std::optional<SyncParams> syncParams() const override;
            Json::Value debug() const override;

            bool updateProgress();
            void needData();

        protected:
            GstreamerAudioPlayer(std::shared_ptr<Gstreamer> gstreamer, const AudioPlayer::Params& params);

            int position() const override {
                return position_.load();
            }
            int duration() const override {
                return duration_.load();
            }
            virtual gboolean handleBusMessage(GstMessage* message);

        protected:
            std::atomic<bool> stopped_{false};
            GstElement* pipeline_ = nullptr;

        private:
            class VolumeController final {
            public:
                explicit VolumeController(GstElement* pipeline);
                ~VolumeController();

                bool setVolume(double volume) const;

            private:
                GstElement* gstVolumeElement_{};
                bool elementOwned_{};
            };

            class Equalizer {
            public:
                explicit Equalizer(GstElement* pipeline);
                ~Equalizer();

                bool setNumBands(int n);

                bool setBandParams(int band, double freq, double width, double gain,
                                   std::optional<YandexIO::EqualizerConfig::Band::Type> type);

                bool setPreampGain(double gain);

            private:
                GstElement* equalizer_;
                GstElement* volume_;
            };

            std::unique_ptr<VolumeController> volumeController_;
            std::unique_ptr<Equalizer> equalizer_;

        private:
            GstStateChangeReturn setStatePaused();
            GstStateChangeReturn setStatePlaying();
            const std::string tag_;

            std::shared_ptr<Gstreamer> gstreamer_;

            std::unique_ptr<GMainContext, GMainContextDeleter> mainContext_;
            std::unique_ptr<GMainLoop, GMainLoopDeleter> mainLoop_;
            std::thread mainLoopThread_;
            std::thread progressThread_;

            std::optional<gint> originalChannels_;
            std::optional<guint64> originalChannelMask_;

            std::mutex progressMutex_;
            std::mutex stateCvMutex_;

            quasar::SteadyConditionVariable playStartCondVar_;
            quasar::SteadyConditionVariable progressCondVar_;
            quasar::SteadyConditionVariable stateChangedCondVar_;

            std::atomic<bool> playing_{false};
            std::atomic<bool> wasStarted_{false};
            std::atomic<bool> wasStopped_{false};
            std::atomic<bool> isEndOfStream_{false};

            std::atomic<bool> positionUpdated_{false};
            std::atomic<int> position_ = 0;
            std::atomic<int> duration_ = 0;
            std::atomic<int> pendingSeekMs_{-1};
            std::atomic<Channel> pendingChannel_{Channel::UNDEFINED};
            std::atomic<Channel> activeChannel_{Channel::UNDEFINED};
            std::atomic<bool> durationWasChanged_{false};
            std::optional<bool> durationIsAvailable_;
            std::atomic<bool> asyncDone_{false};
            std::atomic<bool> buffering_{false};
            std::atomic<gint> bufferingPercent_{0};

            void initPipeline(const std::string& pipeline);
            void mainLoop();
            void progressLoop();
            void checkBufferingState(gint lastBufferingPercent);
            bool waitProgressTimeout();
            static gboolean onBusMessage(GstBus* bus, GstMessage* message, gpointer player);

            StreamSrcPtr dataStream_;
            guint64 streamProgressTs_{0};
            std::unique_ptr<GstElement, GstElementDeleter> appSrcElement_;

            const bool syncSlave_ = false;
            bool syncStandalone_ = true; // Synchronous playback not available, most likely no AudioClock set
            guint64 syncBaseTime_ = 0;
            gint64 syncPosition_ = 0;
        };

        class GstreamerAudioPlayerFactory: public AudioPlayerFactory {
            std::shared_ptr<Gstreamer> gstreamer_;

        public:
            GstreamerAudioPlayerFactory(std::shared_ptr<Gstreamer> gstreamer);
            ~GstreamerAudioPlayerFactory();

            std::unique_ptr<AudioPlayer> createPlayer(const AudioPlayer::Params& params) override;

            void configUpdated(const Json::Value& config) override;
        };

    } // namespace gstreamer
} // namespace quasar
