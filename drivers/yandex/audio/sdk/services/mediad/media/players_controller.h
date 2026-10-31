#pragma once

#include <yandex_io/services/mediad/media/players/player.h>
#include <yandex_io/services/mediad/media/players/player_factory.h>
#include <yandex_io/services/mediad/media/players/player_type.h>

#include <yandex_io/libs/audio_player/base/audio_player.h>
#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/equalizer_config/equalizer_config.h>
#include <yandex_io/protos/model_objects.pb.h>
#include <yandex_io/protos/quasar_proto.pb.h>

#include <json/json.h>

#include <array>
#include <memory>
#include <mutex>
#include <string>

namespace quasar {
    /**
     * PlayersController class handle all Music players. Currently there are 3 types of players:
     *      * Music player. It connects to YandexMusic backend via Websocket, receive Stream Url to play
     *                      and play Music stream via Gstreamer
     *      * Radio Player. Receive url as an argument an play Radio Stream via Gstreamer
     * @NOTE: Since quasar devices has some RAM issues PlayersController keep just one Gstreamer player alive, so
     *        less buffers will be stored but unused in RAM.
     *        PlayersController always know who is his current player, so pause, resume, isPlaying, etc.. methods
     *        always are passed to current player.
     *
     *        Play method create player if it doesn't exist yet (also it stops and remove unused players). All options
     *        are passed to target player.
     *
     */
    class PlayersController {
    public:
        PlayersController(
            std::shared_ptr<YandexIO::IDevice> device,
            std::shared_ptr<const PlayerFactory> playerFactory);
        ~PlayersController();

        /**
         * @throw quasar::RequestTimeoutException if request to Ya.Music api took too long
         */
        void play(PlayerType playerType, const Json::Value& options, const std::string& vinsRequestId = "");
        void pause(bool smooth = true, const std::string& vinsRequestId = "");
        void discard();
        void resume(bool smooth = true, const std::string& vinsRequestId = "");
        bool isPlaying();
        void seekTo(int seconds, const std::string& vinsRequestId);
        void seekForward(int seconds, const std::string& vinsRequestId);
        void seekBackward(int seconds, const std::string& vinsRequestId);

        /**
         * @brief Player settings that may change over time, usually they come through syncd
         * @param configs json object or array with players configurations
         */
        void updatePlayerConfigs(const Json::Value& systemConfig);

        void updateEqualizerConfig(const YandexIO::EqualizerConfig& config);

        /**
         * Reduce the volume of music during the assistant speech and other events
         */
        void freeAudioFocus();

        /**
         * Return the volume of music to the normal values after assistant speech or other events finished.
         * @param smoothVolume - if volume should fade in
         */
        void takeAudioFocus(bool smoothVolume = true);

        /**
         * @throw quasar::RequestTimeoutException if request to Ya.Music api took too long
         */
        void processCommand(const std::string& command, const Json::Value& options);

        /**
         * @brief OnStateChange is a callback that provide current player state when it changes
         */
        using OnStateChange = std::function<void(const NAlice::TDeviceState&)>;
        void setOnStateChangeHandler(OnStateChange onStateChange);

        /**
         * @brief onPlayStart is a callback that provide current player state when it start to play
         */
        using OnPlayStart = std::function<void(PlayerType, const std::string&)>;
        void setOnPlayStartHandler(OnPlayStart onPlayStart);

        /**
         * @brief Callback that provides playback error event when something happened with player
         */
        using OnPlaybackError = std::function<void(const std::string&)>;
        void setOnPlaybackError(OnPlaybackError onPlaybackError);

        /**
         * @brief Callback that provides auth error handling
         */
        using OnAuthError = std::function<void()>;
        void setOnAuthError(OnAuthError onAuthError);

        PlayerType playerType() const;

        /**
         * @return - true if has a player that is ready to resume/pause, etc...
         */
        bool hasActivePlayer() const;

        /**
         * Reset current player, so it will be re-created in the future
         */
        void resetCurrentPlayer();

    private:
        /**
         * @brief Proxy method btw player and onStateChange_ cb. This method make sure that there are just one playing
         *        player at the moment
         * @param type - a type of player that called callback. It is used to understand which player is currently playing
         */
        void onStateChangedProxy(PlayerType type);
        void onPlayStartProxy(PlayerType type);
        void onErrorProxy(PlayerType type, Player::Error error);

        void sendPlayerETEEvent(const std::string& eventName, const std::string& vinsRequestId, PlayerType type);

        /**
         * @brief safely stop all players. Also remove all players except CurrentPlayer
         * @param if current player volume should fade out
         */
        void resetPlayers(bool smoothPause = true);
        void submitPlayerState(const std::unique_ptr<Player>& player);

        enum class CallbackType {
            ON_START,
            ON_STATE_CHANGE,
            ON_PLAYBACK_ERROR,
            ON_AUTH_ERROR
        };
        /**
         * @brief Handle Player callback in separate thread. This function make sure that there will be just one
         *        active player (if some player broke in it will pause current player, than send currentPlayer new state
         *        (it should be paused) and only then change currentPlayer to new one(that broke in) and pass broke in
         *        player state to PlayersController user
         * @param pType - Type of player that called callback
         * @param cbType - Type of callback to call (they should have same behaviour, but different callbacks should be called)
         */
        void handlePlayerCallback(PlayerType pType, CallbackType cbType);

        /**
         * @brief same as hasActivePlayer, but do not lock mutex. So should be called under playersMutex_;
         */
        bool hasActivePlayerUnlocked() const;

        /**
         * Init new player
         * @param playerType
         */
        void initPlayer(PlayerType playerType);

        void setCurrentPlayerType(PlayerType playerType);

        static constexpr int pTypeToInt(PlayerType type) {
            return static_cast<int>(type);
        }
        static constexpr int pErrorToInt(Player::Error error) {
            return static_cast<int>(error);
        }

        std::array<std::unique_ptr<Player>, static_cast<int>(PlayerType::NONE)> players_;
        std::array<Json::Value, static_cast<int>(PlayerType::NONE)> playerConfigs_;
        std::shared_ptr<const PlayerFactory> playerFactory_;

        PlayerType currentPlayerType_ = PlayerType::NONE;
        OnStateChange onStateChange_;
        OnPlayStart onPlayStart_;
        OnPlaybackError onPlaybackError_;
        OnAuthError onAuthError_;
        mutable std::mutex playersMutex_;
        Json::Value extraPlaybackParams_;

        bool ownsFocus_ = true; // Whether we currently own audio focus or we freed it for TTS or other events

        /* Avoid the acquiring of playersMutex_ in one thread. So put all stateChanged callbacks into queue */
        NamedCallbackQueue stateChangedCbQueue_{"PlayersController"};

        std::shared_ptr<YandexIO::IDevice> device_;

        YandexIO::EqualizerConfig equalizerConfig_;
    };
} // namespace quasar
