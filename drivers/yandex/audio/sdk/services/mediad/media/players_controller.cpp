#include "players_controller.h"

#include <yandex_io/libs/ete_metrics/ete_util.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <memory>
#include <sstream>

YIO_DEFINE_LOG_MODULE("media");

using namespace quasar;

PlayersController::PlayersController(
    std::shared_ptr<YandexIO::IDevice> device,
    std::shared_ptr<const PlayerFactory> playerFactory)
    : playerFactory_(std::move(playerFactory))
    , device_(std::move(device))
{
}

PlayersController::~PlayersController() {
    /* Destroy callback queue to avoid any callbacks calls */
    stateChangedCbQueue_.destroy();

    std::lock_guard<std::mutex> guard(playersMutex_);
    for (auto& player : players_) {
        player.reset(nullptr);
    }
}

/* Should be called under playersMutex_ */
void PlayersController::resetPlayers(bool smoothPause) {
    YIO_LOG_DEBUG("Reset players");
    for (auto& player : players_) {
        if (player) {
            player->pause(smoothPause);
            /* Player is paused. Need to capture player state before removing. Otherwise MediaEndpoint won't receive
             * new state with this player paused
             */
            submitPlayerState(player);

            /* Do not remove Current player, because it will be used */
            if (player->type() != currentPlayerType_) {
                player.reset(nullptr);
            }
        }
    }
}

void PlayersController::submitPlayerState(const std::unique_ptr<Player>& player) {
    const Player::State state = player->getState();
    auto onStateChange = onStateChange_;
    {
        /* Player is paused. Send new state to MediaEndpoint. */
        stateChangedCbQueue_.add([state, onStateChange]() {
            if (onStateChange) {
                onStateChange(state.deviceState);
            }
        });
    }
}

void PlayersController::handlePlayerCallback(PlayerType pType, CallbackType cbType)
{
    /* Check if some player broke in */
    std::unique_lock<std::mutex> lock(playersMutex_);
    std::function<void(const Player::State&)> playerStateChangedCb = nullptr;

    /* Copy callback under mutex, but call without mutex to avoid deadlock */
    switch (cbType) {
        case CallbackType::ON_START: {
            auto playStartCopy = onPlayStart_;
            playerStateChangedCb = [pType, playStartCopy](const Player::State& state) {
                if (playStartCopy) {
                    playStartCopy(pType, state.vinsRequestId);
                }
            };
            break;
        }
        case CallbackType::ON_STATE_CHANGE: {
            auto onStateChangedCopy = onStateChange_;
            playerStateChangedCb = [onStateChangedCopy](const Player::State& state) {
                if (onStateChangedCopy) {
                    onStateChangedCopy(state.deviceState);
                }
            };
            break;
        }
        case CallbackType::ON_PLAYBACK_ERROR: {
            /* need to copy callback in order to capture it in lambda */
            auto errorCopy = onPlaybackError_;
            playerStateChangedCb = [errorCopy](const Player::State& state) {
                if (errorCopy) {
                    errorCopy(state.vinsRequestId);
                }
            };
            break;
        }
        case CallbackType::ON_AUTH_ERROR: {
            playerStateChangedCb = [this](const Player::State& state) {
                Y_UNUSED(state);
                if (onAuthError_) {
                    onAuthError_();
                }
            };
            break;
        }
    }

    /* Check if new player started to play. In that case need to stop other players and change currentPlayer onto
     * the one that broke in
     */
    if (pType != currentPlayerType_ && players_[pTypeToInt(pType)] && players_[pTypeToInt(pType)]->isPlaying()) {
        YIO_LOG_INFO("Some player broke in. Change current player from: " << pTypeToInt(currentPlayerType_) << " To: " << pTypeToInt(pType));
        /* Change current player. Stop all other players */
        setCurrentPlayerType(pType);
        for (auto& player : players_) {
            if (player && player->type() != pType) {
                player->pause(true);
                /* Old player is paused now. Send previous player state to MediaEndpoint first. It is necessary
                 * in order to avoid having 2 players PLAYING. Prefer to have all players paused first and only
                 * after that notify MediaEndpoint that some Player broke in
                 */
                if (playerStateChangedCb) {
                    const auto state = player->getState();
                    lock.unlock();
                    playerStateChangedCb(state);
                    lock.lock();
                }
            }
        }
    }
    Player::State currentPlayerState;
    if (players_[pTypeToInt(pType)]) {
        currentPlayerState = players_[pTypeToInt(pType)]->getState();
    }
    lock.unlock();

    if (playerStateChangedCb) {
        playerStateChangedCb(currentPlayerState);
    }
}

void PlayersController::onStateChangedProxy(PlayerType type) {
    YIO_LOG_TRACE("Callback: " << __func__ << " Player: " << pTypeToInt(type));
    /* Handle state in separate thread, because player can call it's callback in same thread where playerMutex is already
     * captured
     */
    stateChangedCbQueue_.add([=]() {
        handlePlayerCallback(type, CallbackType::ON_STATE_CHANGE);
    });
}

void PlayersController::onPlayStartProxy(PlayerType type) {
    YIO_LOG_TRACE("Callback: " << __func__ << " Player: " << pTypeToInt(type));
    /* Handle state in separate thread, because player can call it's callback in same thread where playerMutex is already
     * captured
     */
    stateChangedCbQueue_.add([=]() {
        handlePlayerCallback(type, CallbackType::ON_START);
    });
}

void PlayersController::onErrorProxy(PlayerType type, Player::Error error) {
    YIO_LOG_TRACE("Callback: " << __func__ << " Player: " << pTypeToInt(type) << " Error: " << pErrorToInt(error));
    /* Handle state in separate thread, because player can call it's callback in same thread where playerMutex is already
     * captured
     */
    stateChangedCbQueue_.add([=]() {
        switch (error) {
            case Player::Error::PLAYBACK:
                handlePlayerCallback(type, CallbackType::ON_PLAYBACK_ERROR);
                break;
            case Player::Error::AUTH:
                handlePlayerCallback(type, CallbackType::ON_AUTH_ERROR);
                break;
            default:
                YIO_LOG_WARN("Unknown player error: " << pErrorToInt(error));
        }
    });
}

void PlayersController::sendPlayerETEEvent(const std::string& eventName, const std::string& vinsRequestId, PlayerType type) {
    Json::Value payload = makeETEEvent(vinsRequestId);
    payload["player_type"] = playerTypeToString(type);
    device_->telemetry()->reportEvent(eventName, jsonToString(payload));
}

void PlayersController::play(PlayerType playerType, const Json::Value& options, const std::string& vinsRequestId) {
    std::lock_guard lock(playersMutex_);

    Y_VERIFY(playerType != PlayerType::NONE);

    if (currentPlayerType_ != playerType) {
        setCurrentPlayerType(playerType);
        /* Player changed. Remove Unused players */
        resetPlayers();
        initPlayer(playerType);
    }
    players_[pTypeToInt(playerType)]->play(options);
    sendPlayerETEEvent("players_controller_play", vinsRequestId, playerType);
}

void PlayersController::pause(bool smooth, const std::string& vinsRequestId) {
    std::lock_guard lock(playersMutex_);
    if (hasActivePlayerUnlocked()) {
        players_[pTypeToInt(currentPlayerType_)]->pause(smooth, vinsRequestId);
        sendPlayerETEEvent("players_controller_pause", vinsRequestId, currentPlayerType_);
    }
}

void PlayersController::discard() {
    std::lock_guard lock(playersMutex_);
    if (hasActivePlayerUnlocked()) {
        players_[pTypeToInt(currentPlayerType_)]->discard();
    }
}

void PlayersController::resume(bool smooth, const std::string& vinsRequestId) {
    std::lock_guard lock(playersMutex_);
    if (hasActivePlayerUnlocked()) {
        players_[pTypeToInt(currentPlayerType_)]->resume(smooth, vinsRequestId);
        sendPlayerETEEvent("players_controller_resume", vinsRequestId, currentPlayerType_);
    }
}

bool PlayersController::isPlaying() {
    std::lock_guard lock(playersMutex_);
    if (hasActivePlayerUnlocked()) {
        return players_[pTypeToInt(currentPlayerType_)]->isPlaying();
    }
    return false;
}

void PlayersController::seekTo(int seconds, const std::string& vinsRequestId) {
    std::lock_guard lock(playersMutex_);
    if (hasActivePlayerUnlocked()) {
        players_[pTypeToInt(currentPlayerType_)]->seekTo(seconds);
        sendPlayerETEEvent("players_controller_seek_to", vinsRequestId, currentPlayerType_);
    }
}

void PlayersController::seekForward(int seconds, const std::string& vinsRequestId) {
    std::lock_guard lock(playersMutex_);
    if (hasActivePlayerUnlocked()) {
        players_[pTypeToInt(currentPlayerType_)]->seekForward(seconds);
        sendPlayerETEEvent("players_controller_seek_forward", vinsRequestId, currentPlayerType_);
    }
}

void PlayersController::seekBackward(int seconds, const std::string& vinsRequestId) {
    std::lock_guard lock(playersMutex_);
    if (hasActivePlayerUnlocked()) {
        players_[pTypeToInt(currentPlayerType_)]->seekBackward(seconds);
        sendPlayerETEEvent("players_controller_seek_backward", vinsRequestId, currentPlayerType_);
    }
}

void PlayersController::takeAudioFocus(bool smoothVolume) {
    std::lock_guard lock(playersMutex_);
    ownsFocus_ = true;
    for (auto& player : players_) {
        if (player) {
            player->takeAudioFocus(smoothVolume);
        }
    }
}

void PlayersController::updatePlayerConfigs(const Json::Value& systemConfig)
{
    std::vector<PlayerType> changedConfigs;

    std::lock_guard lock(playersMutex_);

    auto audioClientConfig = tryGetJson(systemConfig, "audioclient", Json::Value());
    extraPlaybackParams_ = tryGetJson(audioClientConfig, "extraPlaybackParams", Json::Value());
    constexpr std::pair<PlayerType, const char*> plist[] = {
        {PlayerType::YANDEX_MUSIC, "YandexMusic"},
        {PlayerType::YANDEX_RADIO, "YandexRadio"},
    };
    for (const auto& [playerType, playerName] : plist)
    {
        auto index = pTypeToInt(playerType);
        playerConfigs_[index] = tryGetJson(systemConfig, playerName);
        auto config = extraPlaybackParams_;
        jsonMerge(playerConfigs_[index], config);
        auto& player = players_[index];
        if (player) {
            auto res = player->updateConfig(config);
            if (res == Player::ChangeConfigResult::NEED_RECREATE) {
                player->pause();
                submitPlayerState(player);
                if (currentPlayerType_ == playerType) {
                    YIO_LOG_INFO("Settings changes for " << playerName << ", invalidate currently playing player");
                    player->discard();
                    setCurrentPlayerType(PlayerType::NONE);
                } else {
                    YIO_LOG_INFO("Settings changes for " << playerName << ", invalidate player");
                }
                player.reset(nullptr);
            } else if (res == Player::ChangeConfigResult::CHANGED) {
                YIO_LOG_INFO("Settings changes for " << playerName << ", hot reload");
            }
        }
    }
}

void PlayersController::updateEqualizerConfig(const YandexIO::EqualizerConfig& config) {
    std::lock_guard lock(playersMutex_);

    equalizerConfig_ = config;

    for (auto& player : players_) {
        if (player) {
            player->setEqualizerConfig(equalizerConfig_);
        }
    }
}

void PlayersController::freeAudioFocus() {
    std::lock_guard lock(playersMutex_);
    ownsFocus_ = false;
    for (auto& player : players_) {
        if (player) {
            player->freeAudioFocus();
        }
    }
}

void PlayersController::processCommand(const std::string& command, const Json::Value& options) {
    std::lock_guard lock(playersMutex_);
    if (hasActivePlayerUnlocked()) {
        players_[pTypeToInt(currentPlayerType_)]->processCommand(command, options);
    }
}

void PlayersController::setOnStateChangeHandler(PlayersController::OnStateChange onStateChange) {
    std::lock_guard lock(playersMutex_);
    onStateChange_ = std::move(onStateChange);
}

void PlayersController::setOnPlayStartHandler(PlayersController::OnPlayStart onPlayStart) {
    std::lock_guard lock(playersMutex_);
    onPlayStart_ = std::move(onPlayStart);
}

void PlayersController::setOnPlaybackError(PlayersController::OnPlaybackError onPlaybackError) {
    std::lock_guard lock(playersMutex_);
    onPlaybackError_ = std::move(onPlaybackError);
}

void PlayersController::setOnAuthError(PlayersController::OnAuthError onAuthError) {
    std::lock_guard lock(playersMutex_);
    onAuthError_ = std::move(onAuthError);
}

PlayerType PlayersController::playerType() const {
    std::lock_guard<std::mutex> guard(playersMutex_);
    return currentPlayerType_;
}

bool PlayersController::hasActivePlayer() const {
    std::lock_guard<std::mutex> guard(playersMutex_);
    return hasActivePlayerUnlocked();
}

bool PlayersController::hasActivePlayerUnlocked() const {
    return currentPlayerType_ != PlayerType::NONE;
}

void PlayersController::resetCurrentPlayer() {
    std::lock_guard lock(playersMutex_);
    if (hasActivePlayerUnlocked()) {
        auto& player = players_[pTypeToInt(currentPlayerType_)];
        if (player) {
            PlayerType type = player->type();
            player.reset(nullptr);
            initPlayer(type);
            submitPlayerState(player);
        }
        YIO_LOG_INFO("PlayersController reset current player")
    }
}

/* Should be called under playersMutex_ */
void PlayersController::initPlayer(PlayerType playerType) {
    /* Avoid recreating already existing player */
    if (!players_[pTypeToInt(playerType)]) {
        auto config = extraPlaybackParams_;
        jsonMerge(playerConfigs_[pTypeToInt(playerType)], config);
        players_[pTypeToInt(playerType)] = playerFactory_->createPlayer(
            playerType, ownsFocus_, config,
            [this, playerType]() { onStateChangedProxy(playerType); },
            [this, playerType]() { onPlayStartProxy(playerType); },
            [this, playerType](Player::Error error) {
                onErrorProxy(playerType, error);
            });
        players_[pTypeToInt(playerType)]->setEqualizerConfig(equalizerConfig_);
    }
}

void PlayersController::setCurrentPlayerType(PlayerType playerType)
{
    currentPlayerType_ = playerType;
}