#include "simple_audio_device_input_controller.h"

#include <yandex_io/libs/logging/logging.h>

namespace YandexIO {

    SimpleAudioDeviceInputController::SimpleAudioDeviceInputController(std::unique_ptr<YandexIO::AudioDevice> audioDevice,
                                                                       std::shared_ptr<YandexIO::IAudioSource> audioSource,
                                                                       std::string mainChannelName, ChannelNameToType typeConverter,
                                                                       std::string vqeType,
                                                                       std::weak_ptr<VqeController> vqeController)
        : audioDevice_(std::move(audioDevice))
        , ioAudioSource_(std::move(audioSource))
        , mainChannelName_(std::move(mainChannelName))
        , typeConverter_(std::move(typeConverter))
        , vqeType_(std::move(vqeType))
        , vqeController_(std::move(vqeController))
    {
        if (typeConverter_ == nullptr) {
            throw std::logic_error("Type convertor should not be nullptr");
        }
        if (audioDevice_ == nullptr) {
            throw std::logic_error("AudioDevice should not be nullptr");
        }
        if (ioAudioSource_ == nullptr) {
            throw std::logic_error("AudioSource should not be nullptr");
        }
        captureThread_ = std::thread([this]() {
            captureThread();
        });
    }

    SimpleAudioDeviceInputController::~SimpleAudioDeviceInputController() {
        stopped_.store(true);
        captureThread_.join();
    }

    void SimpleAudioDeviceInputController::captureThread() {
        while (!stopped_.load()) {
            auto chunks = audioDevice_->capture();
            if (muted_.load()) {
                continue;
            }
            std::string vqeType = vqeType_;
            std::string vqePreset;
            if (auto vqeController = vqeController_.lock()) {
                std::tie(vqeType, vqePreset) = vqeController->getVqeTypeAndPreset();
            }
            ioAudioSource_->pushData(convertAudioDeviceChannels(std::move(chunks), mainChannelName_, typeConverter_,
                                                                audioDevice_->getAvailableChannels(), vqeType, vqePreset));
        }
    }

    void SimpleAudioDeviceInputController::mute() {
        YIO_LOG_INFO("Mute microphones")
        muted_.store(true);
        audioDevice_->stop();
    }

    void SimpleAudioDeviceInputController::unmute() {
        YIO_LOG_INFO("Unmute microphones")
        muted_.store(false);
        audioDevice_->start();
    }

    void SimpleAudioDeviceInputController::setASRMode() {
        YIO_LOG_DEBUG("Set input audio device to ASR mode");
        audioDevice_->setASRMode();
    }

    void SimpleAudioDeviceInputController::setSpotterMode() {
        YIO_LOG_DEBUG("Set input audio device to Spotter mode");
        audioDevice_->setSpotterMode();
    }

} /* namespace YandexIO */
