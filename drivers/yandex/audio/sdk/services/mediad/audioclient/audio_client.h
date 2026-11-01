#pragma once

#include "audio_event_listener.h"
#include "stream_app_src.h"

#include <yandex_io/services/mediad/audio_clock_manager/i_audio_clock_manager.h>
#include <yandex_io/services/mediad/spectrum_listener/spectrum_listener.h>
#include <yandex_io/services/mediad/spectrum_listener/spectrum_provider.h>

#include <yandex_io/interfaces/multiroom/i_multiroom_provider.h>
#include <yandex_io/interfaces/stereo_pair/i_stereo_pair_provider.h>
#include <yandex_io/libs/audio_player/base/audio_player.h>
#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/gogol/gogol_session.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>
#include <yandex_io/protos/model_objects.pb.h>
#include <yandex_io/protos/quasar_proto.pb.h>
#include <yandex_io/sdk/private/device_context.h>

namespace quasar {

    class AudioClient {
    public:
        static const char* PLAY_LATENCY_POINT;

        AudioClient(std::shared_ptr<YandexIO::IDevice> device,
                    YandexIO::DeviceContext& deviceContext,
                    std::shared_ptr<const IAudioClockManager> audioClockManager,
                    std::shared_ptr<AudioPlayerFactory> playerFactory,
                    std::shared_ptr<AudioEventListener> eventListener,
                    Json::Value playbackParams,
                    Json::Value extraPlaybackParams,
                    Json::Value customConfig,
                    const proto::AudioPlayerDescriptor& descriptor,
                    const proto::Audio& audio,
                    std::shared_ptr<gogol::IGogolSession> gogol,
                    std::shared_ptr<YandexIO::SpectrumProvider> spectrumProvider,
                    bool hasAudioFocus);
        ~AudioClient();

        void play(bool hasAudioFocus, bool setPause, AudioPlayer::Channel channel, Json::Value extraAnalytics);
        void pause(bool notifyDeviceContext = false);
        void resume(bool hasAudioFocus);
        void replay(bool hasAudioFocus);
        void freeAudioFocus();
        void takeAudioFocus();

        void rewind(const proto::MediaRequest::Rewind& rewind);
        void pushAudioData(const std::string& data);
        void processEndOfStream();
        void startBuffering();
        void updatePlayerContext(const proto::MediaRequest& request);
        void seek(int seconds);

        /**
         * @brief Set level of volume for this client, relative to current system volume.
         *
         * @param volume - Relative linear coefficient of current system volume in range of [0;10].
         *                  0 -> total silence,
         *                  1 -> current system volume,
         *                  10 -> current system volume amplified 10 times.
         */
        void setVolume(double volume);
        void setSilent(bool silent);

        void setEqualizerConfig(const YandexIO::EqualizerConfig& config);

        void setMetadata(const proto::AudioMetadata& audioMetadata);

        std::chrono::steady_clock::time_point getLastHeartbeatTime() const;

        std::string getFormatForConfig(proto::Audio::Format format) const;

        std::optional<proto::AudioChannel> getAudioChannel() const;

        proto::AudioPlayerDescriptor::PlayerType getDescriptorType() const;

    private:
        class PlayerEventListener;
        // FIXME: ALICE-7011 these methods will be deleted, when all AudioPlayer callbacks will be called in passed ICallbackQueue
        void handleError(const std::string& message);
        void handlePaused();
        void handleResumed();
        void handleStart();
        void handleEnd();
        void handleBufferingStart();
        void handleBufferingEnd();
        void handleProgress(int position, int duration);
        void handleStopped();
        void handleBufferStalled();

        /*
         * Methods to be called only inside commandsQueue_, so no additional synchronisation is required
         */
        void destroyPlayer();
        void switchToEnd();
        void switchToStopped();
        proto::AudioClientEvent switchState(const proto::AudioClientState& toState);
        Json::Value getAnalyticsJson() const;
        proto::AudioClientEvent prepareEvent(proto::AudioClientEvent_Event eventType) const;
        void notifyListener(const proto::AudioClientEvent& event) const;

        int getRewindTarget(const proto::MediaRequest::Rewind& rewind) const;
        void applyAudioFocus(bool hasAudioFocus);
        AudioPlayer::Params getAudioParams(const proto::Audio& audio, const std::shared_ptr<const IAudioClockManager>& audioClockManager);
        void seekTo(int second);
        static std::string stateToString(proto::AudioClientState value);
        static AudioPlayer::Channel convertChannel(StereoPairState::Channel /*channel*/);

    private:
        void reportAnalyticsEvent(const std::string& event);
        void reportEvent(const std::string& event, Json::Value payload);
        void reportPlayLatency(const std::string& eventName, const std::string& eventJson);
        void createPlayLatencyPoint();
        void updateMultiroomSync();

        proto::AudioPlayerDescriptor playerDescriptor_;
        proto::Audio audio_;
        const std::string vsid_;
        std::string tag_;
        bool hasAudioFocus_;
        const bool shouldApplyAudioFocus_;
        const bool reportMetrics_;

        const std::shared_ptr<YandexIO::IDevice> device_;
        const std::shared_ptr<AudioPlayerFactory> playerFactory_;
        const std::shared_ptr<IMultiroomProvider> multiroomProvider_;
        const std::shared_ptr<IStereoPairProvider> stereoPairProvider_;
        const std::shared_ptr<AudioEventListener> eventListener_;
        const Json::Value playbackParams_;
        const Json::Value extraPlaybackParams_;
        const Json::Value customConfig_;
        const bool enablePlayedReporting_;
        const bool useNetClock_{false};

        std::unique_ptr<AudioPlayer> audioPlayer_;
        std::shared_ptr<StreamAppSrc> appsrc_;
        AudioPlayer::Channel channel_{AudioPlayer::Channel::ALL};
        Json::Value extraAnalytics_;

        int64_t lastPlayTimestamp_{0};
        int64_t lastStopTimestamp_{0};
        double currentVolume_{1};
        proto::AudioClientState currentState_ = proto::AudioClientState::IDLE;
        std::atomic<std::chrono::steady_clock::time_point> lastHeartbeatTime_;
        const std::chrono::seconds heartbeatPeriod_{std::chrono::seconds(30)};
        bool forceSendHeartbeat_{false};
        bool fireResumeEvents_{true};
        bool firePauseEvents_{false};
        std::atomic<bool> silent_{false};
        bool setPause_{false};

        const std::shared_ptr<gogol::IGogolSession> gogol_;

        // keep it last, to destroy at the same beginning
        Lifetime lifetime_;
        quasar::NamedCallbackQueue commandsQueue_{"AudioClientCommands"};

        // Compatibility tradeoff to simplify fire.. logic for pause and togglePause events.
        // Will move this fires to aliced::MediaController when clarify mediad issues for fire events.
        YandexIO::DeviceContext& deviceContext_;

        const std::shared_ptr<SpectrumListener> spectrumListener_;
        std::shared_ptr<const YandexIO::LatencyData> playLatencyPoint_;
    };

} // namespace quasar
