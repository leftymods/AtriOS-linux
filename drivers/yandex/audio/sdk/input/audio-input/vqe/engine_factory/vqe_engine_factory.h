#pragma once

#include <yandex_io/modules/audio_input/vqe/engine/vqe_engine.h>

#include <yandex_io/libs/audio/reader/audio_reader_config.h>

#include <json/json.h>

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace YandexIO {

    class VqeEngineFactory {
    public:
        using VqeEnginePtr = std::shared_ptr<YandexIO::VQEEngine>;
        using CreateEngineFunc = std::function<VqeEnginePtr(const Json::Value& config, const std::string& deviceType)>;

        /**
         * @param config - input json config, which is used to create VqeEngine. VqeType depends on
         *                 "VQEtype" field
         */
        [[nodiscard]] VqeEnginePtr createEngine(const Json::Value& config, const std::string& deviceType);

        void addEngineType(const std::string& vqeType, CreateEngineFunc func);

    private:
        std::map<std::string, CreateEngineFunc> factory_;
    };

} /* namespace YandexIO */
