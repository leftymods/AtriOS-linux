#include "vqe_engine_factory.h"

#include <stdexcept>
#include <utility>

namespace YandexIO {

    VqeEngineFactory::VqeEnginePtr VqeEngineFactory::createEngine(const Json::Value& config, const std::string& deviceType) {
        quasar::AudioReaderConfig arConfig(config, deviceType);
        auto it = factory_.find(arConfig.vqeTypeName);
        if (it == factory_.end()) {
            throw std::runtime_error("Can't create vqe engine. Unknown vqeType: " + arConfig.vqeTypeName);
        }
        auto& func = it->second;
        return func(config, deviceType);
    }

    void VqeEngineFactory::addEngineType(const std::string& vqeType, CreateEngineFunc func) {
        const auto res = factory_.insert({vqeType, std::move(func)});
        if (!res.second) {
            std::stringstream ss;
            ss << "Can't add \"" << vqeType << "\" vqeType. Already added";
            throw std::runtime_error(ss.str());
        }
    }

} /* namespace YandexIO */
