#pragma once

#include <yandex_io/modules/audio_input/vqe/engine/vqe_engine.h>
#include <yandex_io/modules/audio_input/vqe/engine_factory/vqe_engine_factory.h>

#include <mutex>

namespace YandexIO {

    class VqeController {
    public:
        using EngineFactory = std::tuple<std::string, VqeEngineFactory::CreateEngineFunc>;
        using EnginesList = std::vector<EngineFactory>;

        VqeController(std::string deviceType, const EnginesList& engines = {});
        std::shared_ptr<YandexIO::VQEEngine> createEngine(const Json::Value& config);
        std::shared_ptr<YandexIO::VQEEngine> getEngine() const;
        void setEngine(std::shared_ptr<YandexIO::VQEEngine> engine, const std::string& type, const std::string& preset);
        void setEngine(const Json::Value& config);

        void setOmniMode(bool omni);
        void setSpeakerVolume(int aliceVolume);

        std::tuple<std::string, std::string> getVqeTypeAndPreset() const;

    private:
        const std::string deviceType_;
        mutable std::mutex mutex_;
        Json::Value lastConfig_;
        bool omni_{false};
        int aliceVolume_{5};
        YandexIO::VqeEngineFactory vqeFactory_;
        std::shared_ptr<YandexIO::VQEEngine> engine_;
        std::string vqeType_;
        std::string vqePreset_;

        void setEngineUnlocked(std::shared_ptr<YandexIO::VQEEngine> engine);
    };

} /* namespace YandexIO */
