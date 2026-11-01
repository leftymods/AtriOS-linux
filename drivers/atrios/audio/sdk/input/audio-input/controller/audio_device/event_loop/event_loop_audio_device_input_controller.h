#pragma once

#include <yandex_io/modules/audio_input/controller/audio_device/utils/utils.h>

#include <yandex_io/modules/audio_input/controller/i_audio_input_controller.h>

#include <yandex_io/modules/audio_input/audio_device/audio_device.h>

#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/sdk/audio_source/i_audio_source.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace YandexIO {

    /**
     * @brief AudioInputController implementation that provide API to change AudioDevice in runtime
     * @NOTE: All api methods and data AudioDevice::capture are called from internal EventLoop.
     *        AudioDevice::capture should hang up without EventLoopAudioDeviceInputController::mute
     *        call, otherwise internal event loop will blocked and there will be no guarantee for a proper
     *        work. So, in case if you are using Blocking alsa mode and when you mute hardware mics ->
     *        alsaRead blocks instead of returning Zeroes --> do not use this class
     */
    class EventLoopAudioDeviceInputController final: public YandexIO::IAudioInputController {
    public:
        using AudioDevicePtr = std::unique_ptr<YandexIO::AudioDevice>;
        // todo: add onOverflow callbacks
        using CreateAudioDeviceFunc = std::function<AudioDevicePtr()>;

        struct AudioDeviceFactory {
            std::string mainChannelName;      /* Channel that will be used for Recognition */
            std::string vqeType;              /* Type of VQE for VQE channel */
            std::string vqePreset;            /* Preset of VQE if present otherwise empty */
            CreateAudioDeviceFunc createFunc; /* Callback that create AudioDevice */
            ChannelNameToType typeConverter;  /* Converter from channel name to YandexIO::ChannelsData::Type */
        };

        /**
         * @param audioSource - YandexIO AudioSource implementation. Will be accessed from internal event loop
         * @param defaultFactory Default factory that will be used from start and if factory from "changeAudioDevice"
         *                       method will throw exception
         */
        EventLoopAudioDeviceInputController(std::shared_ptr<YandexIO::IAudioSource> audioSource,
                                            AudioDeviceFactory defaultFactory);
        ~EventLoopAudioDeviceInputController();

        void setASRMode() override;
        void setSpotterMode() override;

        /**
         * @brief Mute audio device and stop capture scheduling
         */
        void mute() override;
        /**
         * @brief Unmute audio device and schedule capture
         */
        void unmute() override;

        /**
         * @brief Async method that will change AudioDevice at next event loop iteration.
         * @param factory - Factory that create new AudioDevice. If factory.createFunc() throws exception ->
         *                  AudioInputController will fallback to default AudioDeviceFactory (passed to constructor)
         */
        void changeAudioDevice(AudioDeviceFactory factory);

    private:
        void changeAudioDeviceAndApplyState(const AudioDeviceFactory& factory);
        void capture();
        void scheduleCapture();

        enum class Mode {
            SPOTTER,
            ASR
        };
        Mode mode_{Mode::ASR};

        const AudioDeviceFactory defaultFactory_;
        AudioDeviceFactory currentFactory_;
        std::shared_ptr<YandexIO::AudioDevice> audioDevice_;
        std::shared_ptr<YandexIO::IAudioSource> ioAudioSource_;
        bool muted_{true};
        quasar::NamedCallbackQueue worker_{"EventLoopAudioDeviceInputController"};
    };

} /* namespace YandexIO */
