#pragma once

#include "audio_clock.h"
#include "stream_src.h"

#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/equalizer_config/equalizer_config.h>

#include <json/json.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace quasar {

    class AudioPlayer {
    public:
        enum class Format {
            FMT_WAV,
            FMT_MP3,
            FMT_RAW,
        };

        enum class Channel {
            UNDEFINED,
            ALL,
            RIGHT,
            LEFT,
        };
        static Channel parseChannel(std::string_view text);
        static const char* channelName(Channel /*channel*/);

        class Listener {
        public:
            struct SpectrumFrame {
                std::chrono::nanoseconds runningTime;
                std::chrono::nanoseconds duration;
                int rate;
                std::vector<float> magnitudes;
                int threshold;
            };

            virtual ~Listener() = default;

            virtual void onError(const std::string& message) = 0;
            virtual void onPaused() = 0;
            virtual void onResumed() = 0;
            virtual void onStart() = 0;
            virtual void onEnd() = 0;
            virtual void onSeeked() = 0;
            virtual void onProgress(int position, int duration) = 0;
            virtual void onBufferingStart() = 0;
            virtual void onBufferingEnd() = 0;
            virtual void onStopped() = 0;
            virtual void onSpectrum(const SpectrumFrame& frame) = 0;
            virtual void onBufferStalled() = 0;
        };

        class SimpleListener: public Listener {
        public:
            void onError(const std::string& message) override;
            void onPaused() override;
            void onResumed() override;
            void onStart() override;
            void onEnd() override;
            void onSeeked() override;
            void onProgress(int position, int duration) override;
            void onBufferingStart() override;
            void onBufferingEnd() override;
            void onStopped() override;
            void onSpectrum(const SpectrumFrame& frame) override;
            void onBufferStalled() override;
        };

        class Params {
        public:
            enum class Mode {
                MASTER,
                SLAVE,
            };

            virtual ~Params() = default;

            Params& fromConfig(const Json::Value& config);

            Params& setAlsaDevice(const std::string& deviceName);
            Params& setAlsaDevice(const std::string& deviceName, int cardNumber, int deviceNumber);

            Params& setOneShotMode(bool oneShotMode);
            Params& setURI(const std::string& uri);
            Params& setFilePath(const std::string& path);
            Params& setGstreamerPipeline(const std::string& pipeline); /* Gstreamer specific*/
            Params& setStreamSrc(StreamSrcPtr streamSrc);
            Params& setInitialSeekMs(int ms);
            Params& setSoupHttpSrcConfig(const std::map<std::string, std::string>& config);
            Params& setSinkOptionsJson(const Json::Value& options);
            Params& setSinkOptions(const std::map<std::string, std::string>& options);
            Params& setAudioClock(std::shared_ptr<const AudioClock> audioClock);
            Params& setMode(Mode mode);
            Params& setChannel(Channel channel);
            Params& setIsStreamMode(bool isStreamMode);

            struct Normalization {
                double truePeak;
                double integratedLoudness;
                static constexpr double DEFAULT_TARGET_LUFS = -14.0;
                double targetLufs = DEFAULT_TARGET_LUFS;
            };

            Params& setNormalization(const Normalization& settings);

        public:
            const std::string& alsaDevice() const;

            const std::string& uri() const;
            const std::string& filePath() const;
            const std::string& gstPipeline() const;

            const std::string& gstPipelineProcessed() const;
            const std::optional<Normalization>& normalization() const;
            bool onShotMode() const;
            int initialOffsetMs() const;
            std::shared_ptr<const AudioClock> audioClock() const;
            Mode mode() const;
            Channel channel() const;
            StreamSrcPtr streamSrc() const;
            bool isStreamMode() const;
            int bufferStalledThrottle() const;

        protected:
            virtual void initGstreamerPipeline(const Json::Value& config);

        private:
            std::string deviceName_;
            std::string uri_;
            std::string filePath_;
            std::map<std::string, std::string> soupHttpSrcConfig_;
            std::map<std::string, std::string> sinkOptions_;
            int initialSeekMs_ = -1;
            bool isStreamMode_ = false;
            std::string defaultVolumeElement_;
            std::string inputStreamMediaType_;

            StreamSrcPtr streamSrc_;
            std::string gstPipeline_;
            std::string gstPipelineProcessed_;
            bool onShotMode_ = true;
            std::shared_ptr<const AudioClock> audioClock_;
            Mode mode_{Mode::MASTER};
            Channel channel_{Channel::UNDEFINED};
            int bufferStalledThrottle_ = 1;
            std::optional<Normalization> normalization_;
        };

    public:
        AudioPlayer(const Params& params)
            : params_(params)
        {
        }

        virtual ~AudioPlayer();

        virtual bool playAsync() = 0;
        virtual bool replayAsync() = 0;
        virtual bool pause() = 0;
        virtual bool seek(int ms) = 0;
        virtual bool stop() = 0;

        virtual bool playMultiroom(std::chrono::nanoseconds basetime, std::chrono::nanoseconds position);

        virtual bool isPlaying() const = 0;
        virtual bool startBuffering() = 0;
        virtual bool setVolume(double volume) = 0;
        virtual Channel channel();
        virtual bool setChannel(Channel /*ch*/);

        virtual bool setEqualizerConfig(const YandexIO::EqualizerConfig& /*config*/) {
            return false;
        }

        virtual const std::vector<Format>& supportedFormats() const = 0;

        struct SyncParams {
            bool isPlaying;
            std::chrono::nanoseconds basetime;
            std::chrono::nanoseconds position;
        };
        virtual std::optional<SyncParams> syncParams() const {
            return std::nullopt;
        }

        virtual Json::Value debug() const {
            return {};
        }

    public:
        bool isFormatSupported(Format fmt) const {
            return std::find(supportedFormats().begin(),
                             supportedFormats().end(), fmt) != supportedFormats().end();
        };

        void addListener(std::shared_ptr<AudioPlayer::Listener> listener);
        void removeListeners();
        const Params& params() const {
            return params_;
        }

    protected:
        virtual int position() const {
            return 0;
        };
        virtual int duration() const {
            return 0;
        };

        void sendEnd() const;
        void sendStart() const;
        void sendSeeked() const;
        void sendPaused() const;
        void sendResumed() const;
        void sendError(const std::string& message) const;
        void sendProgress() const;
        void sendBufferingStart() const;
        void sendBufferingEnd() const;
        void sendStopped() const;
        void sendSpectrum(const Listener::SpectrumFrame& frame) const;
        void sendBufferStalled();

        Params params_;

    private:
        std::vector<std::shared_ptr<AudioPlayer::Listener>> listeners_;
        mutable NamedCallbackQueue commandsQueue_{"AudioPlayer"};
        int bufferStalledEvents_ = 0;
    };

    class AudioPlayerFactory {
    public:
        virtual ~AudioPlayerFactory() = default;
        virtual std::unique_ptr<AudioPlayer> createPlayer(const AudioPlayer::Params& params) = 0;
        virtual std::unique_ptr<AudioPlayer::Params> createParams() {
            return std::make_unique<AudioPlayer::Params>();
        }
        virtual void configUpdated(const Json::Value& /*config*/) {
        }
    };

    class AudioListenerInjector: public AudioPlayerFactory {
    public:
        using ListenerFactory = std::function<std::vector<std::shared_ptr<AudioPlayer::Listener>>()>;
        AudioListenerInjector(
            std::unique_ptr<AudioPlayerFactory> playerFactory,
            ListenerFactory listenerFactory);

        std::unique_ptr<AudioPlayer> createPlayer(const AudioPlayer::Params& params) override;
        std::unique_ptr<AudioPlayer::Params> createParams() override;

        void configUpdated(const Json::Value& config) override;

    private:
        std::unique_ptr<AudioPlayerFactory> playerFactory_;
        ListenerFactory listenerFactory_;
    };

} // namespace quasar
