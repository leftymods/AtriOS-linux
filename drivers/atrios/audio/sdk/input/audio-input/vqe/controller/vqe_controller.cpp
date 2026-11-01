#include "vqe_controller.h"

#include <yandex_io/libs/json_utils/json_utils.h>

namespace YandexIO {

    VqeController::VqeController(std::string deviceType, const std::vector<EngineFactory>& engines)
        : deviceType_(std::move(deviceType))
        , lastConfig_(Json::objectValue)
    {
        for (const auto& [name, factory] : engines) {
            vqeFactory_.addEngineType(name, factory);
        }
    }

    std::shared_ptr<YandexIO::VQEEngine> VqeController::getEngine() const {
        std::scoped_lock guard(mutex_);
        return engine_;
    }

    std::shared_ptr<YandexIO::VQEEngine> VqeController::createEngine(const Json::Value& config) {
        return vqeFactory_.createEngine(config, deviceType_);
    }

    void VqeController::setEngine(const Json::Value& config) {
        std::scoped_lock guard(mutex_);
        if (config == lastConfig_) {
            return;
        }
        setEngineUnlocked(createEngine(config));
        lastConfig_ = config;

        quasar::AudioReaderConfig arConfig(config, deviceType_);
        vqeType_ = arConfig.vqeTypeName;
        vqePreset_ = arConfig.preset;
    }

    void VqeController::setEngine(std::shared_ptr<YandexIO::VQEEngine> engine, const std::string& type,
                                  const std::string& preset) {
        std::scoped_lock guard(mutex_);
        vqeType_ = type;
        vqePreset_ = preset;
        setEngineUnlocked(engine);
    }

    void VqeController::setEngineUnlocked(std::shared_ptr<YandexIO::VQEEngine> engine) {
        if (engine == nullptr) {
            throw std::runtime_error("Engine should not be nullptr");
        }
        engine->setOmniMode(omni_);
        engine->setSpeakerVolume(aliceVolume_);
        engine_ = std::move(engine);
    }

    void VqeController::setOmniMode(bool omni) {
        std::scoped_lock guard(mutex_);
        omni_ = omni;
        if (engine_) {
            engine_->setOmniMode(omni);
        }
    }

    void VqeController::setSpeakerVolume(int aliceVolume) {
        std::scoped_lock guard(mutex_);
        aliceVolume_ = aliceVolume;
        if (engine_) {
            engine_->setSpeakerVolume(aliceVolume_);
        }
    }

    std::tuple<std::string, std::string> VqeController::getVqeTypeAndPreset() const {
        std::scoped_lock guard(mutex_);

        return {vqeType_, vqePreset_};
    }

} /* namespace YandexIO */
