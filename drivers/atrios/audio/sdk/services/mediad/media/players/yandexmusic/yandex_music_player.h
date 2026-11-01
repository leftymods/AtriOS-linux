#pragma once

#include "request_timeout_exception.h"
#include "yandex_music.h"

#include <yandex_io/services/mediad/media/players/player.h>
#include <yandex_io/services/mediad/spectrum_listener/spectrum_provider.h>
#include <yandex_io/services/mediad/audio_clock_manager/i_audio_clock_manager.h>

#include <yandex_io/interfaces/multiroom/i_multiroom_provider.h>
#include <yandex_io/interfaces/stereo_pair/i_stereo_pair_provider.h>
#include <yandex_io/interfaces/user_config/i_user_config_provider.h>
#include <yandex_io/libs/audio_player/base/audio_clock.h>
#include <yandex_io/libs/audio_player/base/audio_player.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>
#include <yandex_io/sdk/private/device_context.h>

#include <optional>

namespace quasar {
    class YandexMusicPlayer: public Player {
    public:
        YandexMusicPlayer(std::shared_ptr<YandexIO::IDevice> device,
                          std::shared_ptr<ipc::IIpcFactory> ipcFactory,
                          std::shared_ptr<const IAudioClockManager> audioClockManager,
                          bool sslVerification, bool ownsFocus,
                          std::shared_ptr<AudioPlayerFactory> playerFactory,
                          std::shared_ptr<IMultiroomProvider> multiroomProvider,
                          std::shared_ptr<IStereoPairProvider> stereoPairProvider,
                          std::shared_ptr<IUserConfigProvider> userConfigProvider,
                          const Json::Value& customYandexMusicConfig,
                          OnStateChange onStateChange,
                          OnPlayStartChange onStart,
                          OnError onError);

        YandexMusicPlayer(std::shared_ptr<YandexIO::IDevice> device, std::shared_ptr<ipc::IIpcFactory> ipcFactory, bool sslVerification, bool ownsFocus,
                          std::shared_ptr<AudioPlayerFactory> playerFactory, const Json::Value& customYandexMusicConfig);

        ~YandexMusicPlayer();

        /**
         * @throw quasar::RequestTimeoutException if request to Ya.Music api took too long
         */
        void play(const Json::Value& options) override;
        void pause(bool smooth, const std::string& vinsRequestId) override;
        void discard() override;
        void resume(bool smooth, const std::string& vinsRequestId) override;
        bool isPlaying() const override;
        void seekTo(int seconds) override;
        void seekForward(int seconds) override;
        void seekBackward(int seconds) override;

        void takeAudioFocus(bool smoothVolume) override;
        void freeAudioFocus() override;
        bool hasAudioFocus() const override;

        void setEqualizerConfig(const YandexIO::EqualizerConfig& config) override;

        /**
         * @throw quasar::RequestTimeoutException if request to Ya.Music api took too long
         */
        void processCommand(const std::string& command, const Json::Value& options) override;
        ChangeConfigResult updateConfig(const Json::Value& customYandexMusicConfig) override;

        State getState() override;

        void setCurrentProgress(int position, int duration);
        unsigned int getCurrentPosition() const;
        std::shared_ptr<YandexMusic::Track> getCurrentTrack();
        void onStateChange();
        void handleError();
        void handleStart();
        void handleStop();

        using TimePoint = std::chrono::steady_clock::time_point;
        std::optional<TimePoint> getAndResetLastPlayCommandTimePoint();

        void reportLatencyPause();
        void reportLatencyResume();
        void reportLatencyNext();

    protected:
        std::string getTrackIdInternal() const override;

    private:
        static constexpr int PROGRESS_HEARTBEAT_PERIOD_SEC = 30;
        static constexpr double NO_AUDIO_FOCUS_VOLUME = 0.1;

        std::shared_ptr<YandexMusic::Track> currentTrack_ = nullptr;
        std::unique_ptr<AudioPlayer> currentPlayer_ = nullptr;

        virtual std::unique_ptr<AudioPlayer> createPlayer(const std::shared_ptr<YandexMusic::Track>& track,
                                                          int initialSeekMs);
        void setAuthData(const std::string& uid, const std::string& sessionId, const std::string& token, const std::string& deviceId);
        void setVolume(double volume);
        void setVolumeWithTicksInternal(double volume);
        void setVolumeInternal(double volume);

        enum class SwitchTrackMode {
            NEXT,
            NEXT_WITH_SKIP,
            PREV,
            PLAY,
            NONE
        };
        struct SwitchTrackOptions {
            SwitchTrackMode mode;
            bool pause;
        } switchTrackOptions_{SwitchTrackMode::NONE, false};
        void next(bool skip = true, bool setPause = false, bool ignoreIfNotPlaying = false);
        void prev(bool setPause = false);
        /**
         * Method chooses next track to play. Track then will be properly handled in playLoop
         * @param switchTrackMode
         * @param offsetSec from play() only;
         */
        void switchTrack(SwitchTrackMode switchTrackMode, float offsetSec = -1);
        void like();
        void dislike();

        /**
         * Handles user intent to pause/resume music with volume fade in/out
         * Need this as smooth play/pause are not immediate, so we check playerIntent_ every iteration
         */
        void handlePlayerIntent();
        /**
         * Increases volume on one step in quadratic scale
         * @return true if volume is maximum
         */
        bool smoothVolumeUpTick(int tickIntervalMs);
        /**
         * Decreases volume on one step quadratic scale
         * @return true if volume is minimum
         */
        bool smoothVolumeDownTick(int tickIntervalMs);
        /**
         * Determines if volume change should be smooth
         */
        std::atomic_bool withSmooth_{true};
        /**
         * Need this to set up intermediate smooth volume cause volume scale is not linear
         */
        double smoothTicks_ = 0.0;

        double defaultMaxVolume_ = 1.0;
        double maxVolume_ = 1.0;
        double currentVolume_ = 0.0;
        int oneTimeSeekMs_ = -1;
        std::shared_ptr<YandexMusic> yandexMusic_ = nullptr;

        NAlice::TDeviceState::TMusic buildMusicState(YandexMusic::Track* currentTrack) const;
        ChangeConfigResult updateSouphttpsrcConfig(const Json::Value& customYandexMusicConfig);
        ChangeConfigResult updateFailedRequestsConfig(const Json::Value& customYandexMusicConfig);
        ChangeConfigResult updateAudioClock(const Json::Value& customYandexMusicConfig);
        ChangeConfigResult updateMaxVolume(const Json::Value& customYandexMusicConfig);

        /**
         * Here we create our player, choose track to play and then handle user's intent to pause/resume music
         * Basically, the only thread to work with player
         */
        void playLoop();

        void handleRequestTimeout(const std::string& tag, const quasar::RequestTimeoutException& error);
        void handleMultiroomState(const std::shared_ptr<const MultiroomState>& newMultiroomState);
        void handleStereoPairState(const std::shared_ptr<const StereoPairState>& newStereoPairState);
        AudioPlayer::Channel audioPlayerChannelUnsafe() const;

        Lifetime lifetime_;
        const std::shared_ptr<ipc::IIpcFactory> ipcFactory_;
        const std::shared_ptr<const IAudioClockManager> audioClockManager_;
        const std::shared_ptr<IUserConfigProvider> userConfigProvider_;
        const bool sslVerification_;
        bool useNormalization_{false};
        Json::Value customYandexMusicConfig_;

        std::atomic_bool stopped_{false};
        std::atomic_bool wasStarted_{false};
        std::thread playThread_;

        std::mutex playThreadNotifyMutex_;
        bool notified_{false};
        quasar::SteadyConditionVariable playCondVar_;

        mutable std::mutex playerStateMutex_;

        // This var is used to send music state on pause intent even AudioPlayer didn't actually pause it.
        bool forceSendStateOnPause_{false};

        std::string sessionId_;
        std::string uid_;
        std::string multiroomToken_;
        std::optional<TimePoint> lastPlayCommandTimePoint_;
        std::shared_ptr<const MultiroomState> multiroomState_;
        std::shared_ptr<const StereoPairState> stereoPairState_;

        std::map<std::string, std::string> soupHttpSrcConfig_;
        Json::Value playbackConfig_;
        bool isPlaying_{false};
        std::string playPauseId_;
        std::int64_t lastPlayCommandTs_ = 0;

        void maybeSendProgressToMetrica(int position, int duration);
        void sendHeartbeatToMetrica(int position, int duration);

        void notifyPlayThread();

        void waitPlayThreadNotified();

        void checkLastPlayCommandInternal();
        int failedInRowPlayRequests_ = 0;
        int failedRequestsToNotify_ = 5;
        int failedRequestsToSuicide_ = 0; // 0 == turned off
        bool useNetClock_{false};

        const std::shared_ptr<YandexIO::SpectrumProvider> spectrumProvider_;

        YandexIO::EqualizerConfig equalizerConfig_;

        std::shared_ptr<const YandexIO::LatencyData> nextLatencyPoint_;
        std::shared_ptr<const YandexIO::LatencyData> resumeLatencyPoint_;
        std::shared_ptr<const YandexIO::LatencyData> pauseLatencyPoint_;
    };
} // namespace quasar
