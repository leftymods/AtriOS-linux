#pragma once

#include <yandex_io/libs/json_utils/json_utils.h>

#include <memory>
#include <tuple>

namespace quasar {

    class LedColorConfig {
    public:
        LedColorConfig(const Json::Value& config);
        ~LedColorConfig();

        std::tuple<float, float, float> convertColor(float r, float g, float b);

        class Impl;

    private:
        std::unique_ptr<Impl> impl_;
    };

} // namespace quasar
