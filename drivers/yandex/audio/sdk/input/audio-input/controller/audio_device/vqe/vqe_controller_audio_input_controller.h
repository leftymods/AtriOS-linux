#pragma once

#include <yandex_io/modules/audio_input/controller/audio_device/utils/utils.h>

#include <yandex_io/modules/audio_input/controller/multiple_vqe_controller/i_multiple_vqe_controller.h>

#include <yandex_io/modules/audio_input/audio_device/audio_device.h>
#include <yandex_io/modules/audio_input/vqe/controller/vqe_controller.h>

#include <yandex_io/sdk/audio_source/i_audio_source.h>

#include <json/json.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace YandexIO {

    /* TODO: Extract setVqeType from IMultipleVqeController to separate class and inherit from SimpleAudioDeviceInputController here */
    class VqeControllerAudioInputController: public IMultipleVqeController {
    public:
        using VqeEnginePtr = std::shared_ptr<YandexIO::VQEEngine>;
        using AudioDevicePtr = std::unique_ptr<YandexIO::AudioDevice>;
        using CreateAudioDevice = std::function<AudioDevicePtr(const Json::Value& config, std::shared_ptr<VqeController> vqeController)>;

        /**
         * @param[in] config -- Json that will be passed to createVqeEngine and createAudioDevice methods
         * @param[in] defaultVqeType -- name of vqeType that will be used by default
         */
        VqeControllerAudioInputController(Json::Value config,
                                          std::shared_ptr<VqeController> vqeController,
                                          CreateAudioDevice createAudioDevice,
                                          std::string defaultVqeType,
                                          std::shared_ptr<YandexIO::IAudioSource> audioSource,
                                          std::string mainChannelName,
                                          ChannelNameToType typeConverter);
        ~VqeControllerAudioInputController();

        void mute() override;
        void unmute() override;

        void setASRMode() override;
        void setSpotterMode() override;

        void setActiveVqe(const std::string& vqeType, const Json::Value& config) override;

        std::weak_ptr<VqeController> getVqeController();

    private:
        void captureThread();

        const Json::Value config_;
        const std::string defaultVqeType_;
        Json::Value currentVqeConfig_;
        std::string currentVqeType_;
        std::string currentVqePreset_;
        std::unique_ptr<YandexIO::AudioDevice> audioDevice_;
        std::shared_ptr<VqeController> vqeController_;
        std::shared_ptr<YandexIO::IAudioSource> ioAudioSource_;
        const std::string mainChannelName_;
        const ChannelNameToType typeConverter_;
        std::atomic_bool muted_{true};
        std::atomic_bool stopped_{false};
        std::thread captureThread_;
    };

} /* namespace YandexIO */
