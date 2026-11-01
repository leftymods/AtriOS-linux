#pragma once

#include <yandex_io/modules/audio_input/controller/audio_device/utils/utils.h>

#include <yandex_io/modules/audio_input/controller/i_audio_input_controller.h>

#include <yandex_io/modules/audio_input/audio_device/audio_device.h>
#include <yandex_io/modules/audio_input/vqe/controller/vqe_controller.h>

#include <yandex_io/sdk/audio_source/i_audio_source.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace YandexIO {

    class SimpleAudioDeviceInputController: public IAudioInputController {
    public:
        SimpleAudioDeviceInputController(std::unique_ptr<YandexIO::AudioDevice> audioDevice,
                                         std::shared_ptr<YandexIO::IAudioSource> audioSource,
                                         std::string mainChannelName,
                                         ChannelNameToType typeConverter,
                                         std::string vqeType,
                                         std::weak_ptr<VqeController> vqeController);
        ~SimpleAudioDeviceInputController();

        void mute() override;
        void unmute() override;

        void setASRMode() override;
        void setSpotterMode() override;

    private:
        void captureThread();
        std::unique_ptr<YandexIO::AudioDevice> audioDevice_;
        std::shared_ptr<YandexIO::IAudioSource> ioAudioSource_;
        const std::string mainChannelName_;
        const ChannelNameToType typeConverter_;
        const std::string vqeType_;
        std::atomic_bool muted_{true};
        std::atomic_bool stopped_{false};
        std::thread captureThread_;
        std::weak_ptr<VqeController> vqeController_;
    };

} /* namespace YandexIO */
