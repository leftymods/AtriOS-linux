#pragma once

#include <yandex_io/modules/audio_input/controller/i_audio_input_controller.h>

#include <json/json.h>

#include <atomic>
#include <string>

namespace YandexIO {

    /**
     * @brief Extension for IAudioInputController which provides setter that updates
     *        used vqe type
     */
    class IMultipleVqeController: public IAudioInputController {
    public:
        ~IMultipleVqeController() override;
        /**
         * @param vqeType - name of vqeType (i.e. "yandex"). If vqeType is empty then
         *                  IMultipleVqeController should fallback to default vqe type
         * @param config - configuration of VQE. May be not full because it overrides existing default config
         */
        virtual void setActiveVqe(const std::string& vqeType, const Json::Value& config) = 0;
    };

} /* namespace YandexIO */
