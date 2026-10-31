#pragma once

#include "player_type.h"

#include <yandex_io/libs/audio_player/base/audio_player.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/equalizer_config/equalizer_config.h>

#include <json/json.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace quasar {
    /* Forward declaration for PlayerFactory. */
    class AudioPlayerFactory;

    class Player {
    public:
        enum class Error {
            PLAYBACK,
            AUTH
        };

        struct State {
            NAlice::TDeviceState deviceState;
            std::string vinsRequestId;
        };

        using OnStateChange = std::function<void()>;
        using OnPlayStartChange = std::function<void()>;
        using OnError = std::function<void(Error error)>;

        virtual ~Player();
        virtual void play(const Json::Value& options) = 0;
        virtual void pause(bool smooth = true, const std::string& vinsRequestId = "") = 0;
        virtual void discard() = 0;
        virtual void resume(bool smooth = true, const std::string& vinsRequestId = "") = 0;
        enum class PlayerState {
            PLAYING,
            PAUSED,
            NONE
        };
        virtual bool isPlaying() const = 0;
        virtual void seekTo(int seconds) = 0;
        virtual void seekForward(int seconds) = 0;
        virtual void seekBackward(int seconds) = 0;

        virtual void takeAudioFocus(bool smoothVolume = true) = 0;
        virtual void freeAudioFocus() = 0;
        virtual bool hasAudioFocus() const = 0;

        virtual void setEqualizerConfig(const YandexIO::EqualizerConfig& config);

        virtual void processCommand(const std::string& command, const Json::Value& options) = 0;

        enum class ChangeConfigResult {
            // If different settings require a different reaction level, the resulting
            // reaction will be maximum, therefore it is very important to maintain the
            // correct order in enum list
            NO_CHANGES,    // no any relevant changes
            CHANGED,       // some changes were applied
            NEED_RECREATE, // some changes can be applied only via constructor
        };
        virtual ChangeConfigResult updateConfig(const Json::Value& config);

        PlayerType type() const;

        virtual State getState() = 0;

        /*
         * Reports event to metrica
         * Enriches event with current state, so should be called with appropriate (implementation-defined) mutex held.
         */
        void reportEvent(const std::string& event, bool skipDatabase = false) const;

        void reportEvent(const std::string& event, Json::Value eventJson, bool skipDatabase = false) const;

        /*
         * Reports event to metrica
         * No requirements to call with specific mutex held unlike Player::reportEvent
         */
        void reportEventWithTrackId(const std::string& event,
                                    const std::string& trackId,
                                    bool skipDatabase = false,
                                    bool removeCurrentVinsId = false) const;

        void reportEventWithTrackId(const std::string& event,
                                    Json::Value eventJson,
                                    const std::string& trackId,
                                    bool skipDatabase = false,
                                    bool removeCurrentVinsId = false) const;

    protected:
        Player(std::shared_ptr<YandexIO::IDevice> device,
               OnStateChange onStateChange,
               OnPlayStartChange onStart,
               OnError onError);

        /**
         * track id for music and url of radio stream for radio
         * @return
         */
        virtual std::string getTrackIdInternal() const = 0;

        /**
         * @brief Unified method for all players to get current timestamp
         * @return return current system clock timestamp ms (time since epoch)
         */
        static int64_t getNowMs();

        enum class PlayerIntent {
            RESUME,
            PAUSE,
            NONE
        };
        /**
         * Represents user's intent to put player in some state
         * If none, previous_intent == current_state and user is happy, we need to do nothing
         * Need this as smooth play/pause are not immediate
         */
        std::atomic<PlayerIntent> playerIntent_{PlayerIntent::NONE};

        /**
         * intervals to change volume by one tick
         */
        struct SmoothVolumeInterval {
            int pauseResume;
            int audioFocus;
        };
        SmoothVolumeInterval smoothVolumeIntervalMs_;

        std::shared_ptr<YandexIO::IDevice> device_;
        std::atomic_bool hasAudioFocus_{true};
        std::shared_ptr<AudioPlayerFactory> playerFactory_;
        OnStateChange onStateChange_;
        OnPlayStartChange onStart_;
        OnError onError_;
        PlayerType type_;
        std::string vinsInitRequestId_;
        mutable std::string currentVinsRequestId_;
    };

    namespace commands {
        const std::string LIKE = "like";
        const std::string DISLIKE = "dislike";
        const std::string NEXT = "next";
        const std::string PREVIOUS = "previous";
    } // namespace commands
} // namespace quasar
