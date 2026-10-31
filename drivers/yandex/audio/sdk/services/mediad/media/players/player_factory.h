#pragma once

#include "player.h"

#include <yandex_io/services/mediad/audio_clock_manager/i_audio_clock_manager.h>

#include <yandex_io/interfaces/multiroom/i_multiroom_provider.h>
#include <yandex_io/interfaces/stereo_pair/i_stereo_pair_provider.h>
#include <yandex_io/interfaces/user_config/i_user_config_provider.h>
#include <yandex_io/libs/audio_player/base/audio_clock.h>
#include <yandex_io/libs/audio_player/base/audio_player.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/sdk/private/device_context.h>
#include <yandex_io/sdk/sdk_interface.h>

#include <map>
#include <memory>

namespace quasar {

    class PlayerFactory {
    public:
        using PlayerConstructor =
            std::function<std::unique_ptr<Player>(
                std::shared_ptr<YandexIO::IDevice>,
                std::shared_ptr<ipc::IIpcFactory>,
                std::shared_ptr<const IAudioClockManager>,
                std::shared_ptr<AudioPlayerFactory>,
                bool ownsFocus,
                const Json::Value& customPlayerConfig,
                Player::OnStateChange,
                Player::OnPlayStartChange,
                Player::OnError)>;

        PlayerFactory(
            std::shared_ptr<YandexIO::IDevice> device,
            std::shared_ptr<ipc::IIpcFactory> ipcFactory,
            std::shared_ptr<const IAudioClockManager> audioClockManager,
            std::shared_ptr<AudioPlayerFactory> audioPlayerFactory,
            std::shared_ptr<YandexIO::SDKInterface> sdk);
        void addConstructor(PlayerType /*playerType*/, PlayerConstructor /*playerConstructor*/);
        void addYandexMusicConstructor(
            bool sslVerification,
            std::shared_ptr<IMultiroomProvider> multiroomProvider,
            std::shared_ptr<IStereoPairProvider> stereoPairProvider,
            std::shared_ptr<IUserConfigProvider> userConfigProvider); // Special method added for historical reasons

        static PlayerConstructor buildYandexMusicPlayerConstructor(
            bool sslVerification,
            std::shared_ptr<IMultiroomProvider> multiroomProvider,
            std::shared_ptr<IStereoPairProvider> stereoPairProvider,
            std::shared_ptr<IUserConfigProvider> userConfigProvider);

        void addYandexRadioConstructor(); // Special method added for historical reasons
        static PlayerConstructor buildYandexRadioPlayerConstructor(
            std::shared_ptr<YandexIO::IFilePlayerCapability> filePlayerCapability);

        std::shared_ptr<AudioPlayerFactory> audioPlayerFactory() const;
        std::shared_ptr<const IAudioClockManager> audioClockManager() const;
        std::unique_ptr<Player> createPlayer(
            PlayerType /*playerType*/,
            bool ownsFocus,
            const Json::Value& customPlayerConfig,
            Player::OnStateChange onStateChange,
            Player::OnPlayStartChange onPlayStartChange,
            Player::OnError onError) const;

        std::shared_ptr<ipc::IIpcFactory> ipcFactory() const;

    private:
        const std::shared_ptr<YandexIO::IDevice> device_;
        const std::shared_ptr<ipc::IIpcFactory> ipcFactory_;
        const std::shared_ptr<const IAudioClockManager> audioClockManager_;
        const std::shared_ptr<AudioPlayerFactory> audioPlayerFactory_;
        const std::shared_ptr<YandexIO::SDKInterface> sdk_;
        mutable YandexIO::DeviceContext deviceContext_;

        mutable std::mutex mutex_;
        std::map<PlayerType, PlayerConstructor> constructors_;
    };

} // namespace quasar
