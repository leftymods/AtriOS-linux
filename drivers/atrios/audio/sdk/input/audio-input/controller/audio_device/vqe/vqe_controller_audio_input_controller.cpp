#include "vqe_controller_audio_input_controller.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("audio_input_controller");

namespace {
    Json::Value getSpecificVqeConfig(const Json::Value& config, const std::string& vqeType) {
        return quasar::tryGetJson(quasar::tryGetJson(config, "vqe", Json::objectValue), vqeType, Json::objectValue);
    }
} // namespace

namespace YandexIO {

    VqeControllerAudioInputController::VqeControllerAudioInputController(Json::Value config,
                                                                         std::shared_ptr<VqeController> vqeController,
                                                                         CreateAudioDevice createAudioDevice,
                                                                         std::string defaultVqeType,
                                                                         std::shared_ptr<YandexIO::IAudioSource> audioSource,
                                                                         std::string mainChannelName,
                                                                         ChannelNameToType typeConverter)
        : config_(std::move(config))
        , defaultVqeType_(std::move(defaultVqeType))
        , currentVqeConfig_(Json::objectValue)
        , vqeController_(std::move(vqeController))
        , ioAudioSource_(std::move(audioSource))
        , mainChannelName_(std::move(mainChannelName))
        , typeConverter_(std::move(typeConverter))
    {
        if (typeConverter_ == nullptr) {
            throw std::logic_error("Type convertor should not be nullptr");
        }
        if (ioAudioSource_ == nullptr) {
            throw std::logic_error("AudioSource should not be nullptr");
        }
        if (!vqeController_) {
            throw std::logic_error("VqeController should not be nullptr");
        }
        setActiveVqe(defaultVqeType_, getSpecificVqeConfig(config_, defaultVqeType_));
        audioDevice_ = createAudioDevice(config_, vqeController_);
        if (audioDevice_ == nullptr) {
            throw std::logic_error("AudioDevice should not be nullptr");
        }

        captureThread_ = std::thread([this]() {
            captureThread();
        });
    }

    VqeControllerAudioInputController::~VqeControllerAudioInputController() {
        stopped_.store(true);
        captureThread_.join();
    }

    void VqeControllerAudioInputController::captureThread() {
        while (!stopped_.load()) {
            auto chunks = audioDevice_->capture();
            if (muted_.load()) {
                continue;
            }
            ioAudioSource_->pushData(convertAudioDeviceChannels(std::move(chunks), mainChannelName_, typeConverter_,
                                                                audioDevice_->getAvailableChannels(), currentVqeType_, currentVqePreset_));
        }
    }

    void VqeControllerAudioInputController::mute() {
        YIO_LOG_INFO("Mute microphones")
        muted_.store(true);
        audioDevice_->stop();
    }

    void VqeControllerAudioInputController::unmute() {
        YIO_LOG_INFO("Unmute microphones")
        muted_.store(false);
        audioDevice_->start();
    }

    void VqeControllerAudioInputController::setASRMode() {
        YIO_LOG_DEBUG("Set VqeController to ASR mode");
        vqeController_->setOmniMode(false);
    }

    void VqeControllerAudioInputController::setSpotterMode() {
        YIO_LOG_DEBUG("Set VqeController to Spotter mode");
        vqeController_->setOmniMode(true);
    }

    void VqeControllerAudioInputController::setActiveVqe(const std::string& vqeType, const Json::Value& config) {
        const std::string newVqeType = vqeType.empty() ? defaultVqeType_ : vqeType;
        Json::Value mergedConfig(getSpecificVqeConfig(config_, newVqeType));
        mergeVqeConfigsWithJsonPreset(config, mergedConfig);

        if (newVqeType == currentVqeType_ && mergedConfig == currentVqeConfig_) {
            return;
        }
        YIO_LOG_INFO("Creating VQE with config: " << mergedConfig);
        try {
            vqeController_->setEngine(mergedConfig);
            currentVqeConfig_ = mergedConfig;
            currentVqeType_ = newVqeType;
            currentVqePreset_ = std::get<1>(vqeController_->getVqeTypeAndPreset());
        } catch (const std::exception& e) {
            YIO_LOG_ERROR_EVENT("VqeControllerAudioInputController.SetActiveVqe.Exception", "VQE didnt changed: " << e.what());
        } catch (...) {
            YIO_LOG_ERROR_EVENT("VqeControllerAudioInputController.SetActiveVqe.UnknownError", "VQE didnt changed: unknown exception happened during changing");
        }
    }

    std::weak_ptr<VqeController> VqeControllerAudioInputController::getVqeController() {
        return vqeController_;
    }

} /* namespace YandexIO */
