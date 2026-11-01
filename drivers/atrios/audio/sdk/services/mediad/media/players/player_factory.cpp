#include "player_factory.h"

#include <yandex_io/services/mediad/media/players/yandexmusic/yandex_music_player.h>
#include <yandex_io/services/mediad/media/players/yandexradio2/yandex_radio_player2.h>

#include <yandex_io/services/mediad/spectrum_listener/spectrum_listener.h>

#include <yandex_io/libs/json_utils/json_utils.h>

using namespace quasar;

PlayerFactory::PlayerFactory(
    std::shared_ptr<YandexIO::IDevice> device,
    std::shared_ptr<ipc::IIpcFactory> ipcFactory,
    std::shared_ptr<const IAudioClockManager> audioClockManager,
    std::shared_ptr<AudioPlayerFactory> audioPlayerFactory,
    std::shared_ptr<YandexIO::SDKInterface> sdk)
    : device_(std::move(device))
    , ipcFactory_(std::move(ipcFactory))
    , audioClockManager_(std::move(audioClockManager))
    , audioPlayerFactory_(std::move(audioPlayerFactory))
    , sdk_(std::move(sdk))
    , deviceContext_(ipcFactory_)
{
}

void PlayerFactory::addConstructor(PlayerType playerType, PlayerConstructor playerConstructor)
{
    std::unique_lock<std::mutex> lock(mutex_);
    constructors_[playerType] = std::move(playerConstructor);
}

PlayerFactory::PlayerConstructor PlayerFactory::buildYandexMusicPlayerConstructor(
    bool sslVerification,
    std::shared_ptr<IMultiroomProvider> multiroomProvider,
    std::shared_ptr<IStereoPairProvider> stereoPairProvider,
    std::shared_ptr<IUserConfigProvider> userConfigProvider)
{
    return
        [sslVerification,
         multiroomProvider{std::move(multiroomProvider)},
         stereoPairProvider{std::move(stereoPairProvider)},
         userConfigProvider{std::move(userConfigProvider)}](
            std::shared_ptr<YandexIO::IDevice> device,
            std::shared_ptr<ipc::IIpcFactory> ipcFactory,
            std::shared_ptr<const IAudioClockManager> audioClockManager,
            std::shared_ptr<AudioPlayerFactory> audioPlayerFactory,
            bool ownsFocus,
            const Json::Value& customPlayerConfig,
            Player::OnStateChange onStateChange,
            Player::OnPlayStartChange onPlayStartChange,
            Player::OnError onError)
    {
        return std::make_unique<YandexMusicPlayer>(
            std::move(device),
            std::move(ipcFactory),
            std::move(audioClockManager),
            sslVerification,
            ownsFocus,
            std::move(audioPlayerFactory),
            multiroomProvider,
            stereoPairProvider,
            userConfigProvider,
            customPlayerConfig,
            std::move(onStateChange),
            std::move(onPlayStartChange),
            std::move(onError));
    };
}

void PlayerFactory::addYandexMusicConstructor(
    bool sslVerification,
    std::shared_ptr<IMultiroomProvider> multiroomProvider,
    std::shared_ptr<IStereoPairProvider> stereoPairProvider,
    std::shared_ptr<IUserConfigProvider> userConfigProvider)
{
    addConstructor(
        PlayerType::YANDEX_MUSIC,
        buildYandexMusicPlayerConstructor(
            sslVerification,
            std::move(multiroomProvider),
            std::move(stereoPairProvider),
            std::move(userConfigProvider)));
}

void PlayerFactory::addYandexRadioConstructor()
{
    addConstructor(PlayerType::YANDEX_RADIO,
                   buildYandexRadioPlayerConstructor(sdk_->getFilePlayerCapability()));
}

PlayerFactory::PlayerConstructor PlayerFactory::buildYandexRadioPlayerConstructor(std::shared_ptr<YandexIO::IFilePlayerCapability> filePlayerCapability) {
    return [filePlayerCapability{std::move(filePlayerCapability)}](
               std::shared_ptr<YandexIO::IDevice> device,
               std::shared_ptr<ipc::IIpcFactory> ipcFactory,
               std::shared_ptr<const IAudioClockManager> /*clockManager*/,
               std::shared_ptr<AudioPlayerFactory> audioPlayerFactory,
               bool ownsFocus,
               const Json::Value& customPlayerConfig,
               Player::OnStateChange onStateChange,
               Player::OnPlayStartChange onPlayStartChange,
               Player::OnError onError) -> std::unique_ptr<Player> {
        return std::make_unique<YandexRadioPlayer2>(
            std::move(device),
            std::move(ipcFactory),
            filePlayerCapability,
            ownsFocus,
            std::move(audioPlayerFactory),
            customPlayerConfig,
            std::move(onStateChange),
            std::move(onPlayStartChange),
            std::move(onError));
    };
}

std::shared_ptr<AudioPlayerFactory> PlayerFactory::audioPlayerFactory() const {
    return audioPlayerFactory_;
}

std::shared_ptr<const IAudioClockManager> PlayerFactory::audioClockManager() const {
    return audioClockManager_;
}

std::unique_ptr<Player> PlayerFactory::createPlayer(
    PlayerType playerType,
    bool ownsFocus,
    const Json::Value& customPlayerConfig,
    Player::OnStateChange onStateChange,
    Player::OnPlayStartChange onPlayStartChange,
    Player::OnError onError) const {
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = constructors_.find(playerType);
    if (it == constructors_.end()) {
        return nullptr;
    }
    return it->second(device_, ipcFactory_, audioClockManager_, audioPlayerFactory_, ownsFocus, customPlayerConfig, std::move(onStateChange), std::move(onPlayStartChange), std::move(onError));
}

std::shared_ptr<ipc::IIpcFactory> PlayerFactory::ipcFactory() const {
    return ipcFactory_;
}
