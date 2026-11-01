#include "event_loop_audio_device_input_controller.h"

#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("audio_input_controller");

namespace YandexIO {

    /**
     * AudioDevice has blocking "capture" methods that will wait until it have data to provide
     * After AudioDevice::start audio device will collect data that will be passed to "capture" method
     * After AudioDevice::stop audio device will stop collecting data and if AudioDevice::capture is called
     * at the same time -> capture may block. So need to make sure that "capture" never blocks forever inside
     * event loop.
     * So do not start capture until AudioDevice::start is called (so first capture will be called after explicit "unmute" call)
     * Since it is possible that "capture" will be added after "mute" -> make sure that capture skip actual audioDevice_->capture
     * call and do not hang up (and do not schedule next iteration)
     */

    EventLoopAudioDeviceInputController::EventLoopAudioDeviceInputController(std::shared_ptr<YandexIO::IAudioSource> audioSource,
                                                                             AudioDeviceFactory defaultFactory)
        : defaultFactory_(defaultFactory)
        , currentFactory_(defaultFactory)
        , ioAudioSource_(std::move(audioSource))
    {
        /* start capture thread */
        changeAudioDevice(std::move(defaultFactory));
    }

    EventLoopAudioDeviceInputController::~EventLoopAudioDeviceInputController() {
        worker_.destroy();
    }

    void EventLoopAudioDeviceInputController::changeAudioDevice(AudioDeviceFactory factory) {
        worker_.add([this, factory{std::move(factory)}]() mutable {
            try {
                changeAudioDeviceAndApplyState(factory);
                /* save factory with payload */
                currentFactory_ = std::move(factory);
            } catch (const std::exception& e) {
                YIO_LOG_ERROR_EVENT("EventLoopAudioDeviceInputController.ChangeAudioDevice.Exception", "Can't create custom audio device: " << e.what());
                /* fallback to default and do not try to recover if it fails */
                currentFactory_ = defaultFactory_;
                changeAudioDeviceAndApplyState(defaultFactory_);
            }
        });
    }

    void EventLoopAudioDeviceInputController::changeAudioDeviceAndApplyState(const AudioDeviceFactory& factory) {
        if (audioDevice_) {
            /* make sure that capture thread is stopped */
            audioDevice_->stop();
        }
        audioDevice_.reset(); /* remove old AudioDevice to free resources (alsa, memory, etc) */
        audioDevice_ = factory.createFunc();
        if (muted_) {
            audioDevice_->stop();
        } else {
            audioDevice_->start();
        }
        switch (mode_) {
            case Mode::ASR: {
                audioDevice_->setASRMode();
                break;
            }
            case Mode::SPOTTER: {
                audioDevice_->setSpotterMode();
                break;
            }
        }
    }

    void EventLoopAudioDeviceInputController::setASRMode() {
        worker_.add([this]() {
            if (mode_ != Mode::ASR) {
                mode_ = Mode::ASR;
                audioDevice_->setASRMode();
            }
        });
    }

    void EventLoopAudioDeviceInputController::setSpotterMode() {
        worker_.add([this]() {
            if (mode_ != Mode::SPOTTER) {
                mode_ = Mode::SPOTTER;
                audioDevice_->setSpotterMode();
            }
        });
    }

    void EventLoopAudioDeviceInputController::mute() {
        worker_.add([this]() {
            YIO_LOG_INFO("Mute microphones")
            muted_ = true;
            audioDevice_->stop();
        });
    }

    void EventLoopAudioDeviceInputController::unmute() {
        worker_.add([this]() {
            YIO_LOG_INFO("Unmute microphones")
            muted_ = false;
            audioDevice_->start();
            /* Go to next capture iteration */
            scheduleCapture();
        });
    }

    void EventLoopAudioDeviceInputController::capture() {
        auto chunks = audioDevice_->capture();
        const auto& mainChannelName = currentFactory_.mainChannelName;
        const auto& typeConverter = currentFactory_.typeConverter;
        const auto& vqeType = currentFactory_.vqeType;
        const auto& vqePreset = currentFactory_.vqePreset;
        auto channelsData = convertAudioDeviceChannels(
            std::move(chunks), mainChannelName, typeConverter, audioDevice_->getAvailableChannels(), vqeType, vqePreset);
        ioAudioSource_->pushData(std::move(channelsData));
    }

    void EventLoopAudioDeviceInputController::scheduleCapture() {
        worker_.add([this]() {
            /* Call capture and schedule next capture only if not muted, otherwise capture may hang up
             * and it will block whole EventLoop
             */
            if (!muted_) {
                capture();
                /* move to next iteration */
                scheduleCapture();
            }
        });
    }

} /* namespace YandexIO */
