#pragma once

#include <yandex_io/capabilities/file_player/interfaces/i_file_player_capability.h>
#include <yandex_io/services/mediad/media/players/player.h>
#include <yandex_io/services/mediad/media/players/volume_smoother.h>
#include <yandex_io/services/mediad/spectrum_listener/spectrum_provider.h>
#include <yandex_io/libs/audio_player/base/audio_player.h>
#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>
#include <yandex_io/sdk/private/device_context.h>

#include <json/json.h>

#include <chrono>
#include <mutex>

namespace quasar {
    class YandexRadioPlayer2: public Player {
    public:
        YandexRadioPlayer2(
            std::shared_ptr<YandexIO::IDevice> device,
            std::shared_ptr<ipc::IIpcFactory> ipcFactory,
            std::shared_ptr<YandexIO::IFilePlayerCapability> filePlayerCapability,
            bool ownsFocus,
            std::shared_ptr<AudioPlayerFactory> playerFactory);
        YandexRadioPlayer2(
            std::shared_ptr<YandexIO::IDevice> device,
            std::shared_ptr<ipc::IIpcFactory> ipcFactory,
            std::shared_ptr<YandexIO::IFilePlayerCapability> filePlayerCapability,
            bool ownsFocus,
            std::shared_ptr<AudioPlayerFactory> playerFactory,
            const Json::Value& customPlayerConfig,
            OnStateChange onStateChange,
            OnPlayStartChange onStart,
            OnError onError);
        ~YandexRadioPlayer2();

    public: // Player
        void play(const Json::Value& options) override;
        void pause(bool smooth, const std::string& vinsRequestId) override;
        void discard() override;
        void resume(bool smooth, const std::string& vinsRequestId) override;
        bool isPlaying() const override;
        void seekTo(int ms) override;
        void seekForward(int seconds) override;
        void seekBackward(int seconds) override;
        void takeAudioFocus(bool smooth) override;
        void freeAudioFocus() override;
        bool hasAudioFocus() const override;
        void setEqualizerConfig(const YandexIO::EqualizerConfig& config) override;

        void processCommand(const std::string& command, const Json::Value& options) override;
        ChangeConfigResult updateConfig(const Json::Value& config) override;
        State getState() override;

    protected: // Player
        std::string getTrackIdInternal() const override;

    public:
        void handleError(int playerId, const std::string& error);
        void handleStart(int playerId);
        void handleEnd(int playerId);
        void handleResumed(int playerId);
        void handleProgress(int playerId, int position, int duration);
        void handleBufferingStart(int playerId);
        void handleBufferingEnd(int playerId);

    private:
        void pauseUnsafe(bool smooth);
        void resumeUnsafe(bool smooth);
        void setVolume(float targetVolume, std::chrono::milliseconds period);

        enum class ErrorLevel {
            ERROR,
            FATAL,
        };
        void reportError(ErrorLevel errorLevel, const std::string& errorMessage) const;
        void notifyStateChanged() const; // Can be called from different threads
        static int64_t heartbeat();

    private: // Audio Command Queue method (ACQ)
        void acqEnsureThread() const;
        void acqDestroyPlayer();
        void acqPlay(int playerId, const std::string& url);
        void acqPause();
        virtual std::unique_ptr<AudioPlayer> acqCreatePlayer(int playerId, const std::string& url);
        void acqUpdateVolume();
        enum class PauseType {
            RESTARTING,
            AWAITING,
        };
        void acqProcessUnexpectedPause(int playerId, PauseType /*pauseType*/, const std::string& errorMessage);
        void acqRestartPlayer(int playerId);
        void acqCancelUnexpectedPause(int playerId, const std::string& reason);
        void acqAbortUnexpectedPause(int whiteNoiseId);
        void acqEnableWhiteNoise(int whiteNoiseId, PauseType pauseType, Json::Value whiteNoiseJson);
        void acqDisableWhiteNoise();
        void acqSayLostTheWave();
        void acqHeartbeatMonitor(int playerId);
        google::protobuf::Struct buildRadioState() const;

    private:
        const std::shared_ptr<YandexIO::IFilePlayerCapability> filePlayerCapability_;
        const std::string vsid_;
        const Json::Value playbackConfig_;
        const std::string whiteNoiseGroup_;
        VolumeSmoother volumeSmoother_;
        std::atomic<bool> volumeSmoothing_{false};
        std::atomic<int64_t> heartbeat_{0};

        /*
         * Access to available only from "Player" interface method and must be
         * protected with mutex
         */
        mutable std::mutex mutex_;
        std::shared_ptr<NamedCallbackQueue> audioCommandQueue_;
        std::shared_ptr<NamedCallbackQueue> recyclingPlayerQueue_;
        int playerId_{0};
        bool isPlaying_{false};
        Json::Value options_;
        std::string url_;
        std::int64_t lastPlayCommandTs_ = 0;

        /*
         * Access to this objects must be from audioCommandQueue_ thread (ACQ)
         * NOTE: no any mutex required!
         */
        std::thread::id acqThreadId_;
        int currentPlayerId_{0};
        std::unique_ptr<AudioPlayer> currentPlayer_;
        bool currentFirstRun_{false};
        int currentReportedPosition_{-1};
        int currentWhiteNoiseId_{0};
        Json::Value currentWhiteNoiseJson_{Json::nullValue};
        std::chrono::steady_clock::time_point currentWhiteNoiseBegin_;
        const std::shared_ptr<YandexIO::SpectrumProvider> spectrumProvider_;

        std::shared_ptr<const YandexIO::LatencyData> playRequestLatencyPoint_;
        std::shared_ptr<const YandexIO::LatencyData> pauseRequestLatencyPoint_;

        YandexIO::EqualizerConfig equalizerConfig_;
    };

} // namespace quasar
