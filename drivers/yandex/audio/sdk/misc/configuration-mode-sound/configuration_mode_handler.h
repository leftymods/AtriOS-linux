#pragma once

#include <yandex_io/modules/volume_manager/base/volume_manager.h>

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/sdk/device_mode_observer.h>
#include <yandex_io/sdk/sdk_interface.h>

#include <optional>

class ConfigurationModeHandler: public std::enable_shared_from_this<ConfigurationModeHandler>, public YandexIO::DeviceModeObserver {
public:
    struct Settings {
        std::string enterConfigurationModeSound = "enter_configure.wav";
        std::string enterConfigurationModeFirstTimeSound = "enter_configure.wav";
        int firstInitDefaultVolume = 5;
        int initModeMinimumVolume = 3;
    };

    ConfigurationModeHandler(std::shared_ptr<YandexIO::SDKInterface> sdk,
                             std::shared_ptr<VolumeManager> volumeManager,
                             Settings settings = {"enter_configure.wav", "enter_configure.wav", 5, 3});

    void onStartSetup(bool firstTime) override;
    void onFinishSetup() override;

    void subscribeToSDK();

    void onSoundDataTransferStart() override{};
    void onConnectingToNetwork() override{};
    void onSetupError() override{};
    void onConfigureSuccessUpdate(ConfigurationSuccessState /*configurationSuccessState*/) override{};

private:
    void playConfigurationSound(bool firstTime);

    void playSound(const std::string& wavFile);
    void stopSound();

    std::optional<bool> setupMode_ = std::nullopt;
    std::string currentSound_;

    const Settings settings_;
    std::shared_ptr<YandexIO::SDKInterface> sdk_;
    const std::shared_ptr<VolumeManager> volumeManager_;
};
