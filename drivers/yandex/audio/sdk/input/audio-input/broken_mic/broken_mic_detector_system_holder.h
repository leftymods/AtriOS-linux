#pragma once

#include "broken_mic_config_handler.h"
#include "broken_mic_first_configure_handler.h"
#include "broken_mic_logger.h"

#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/sdk/sdk_interface.h>
#include <yandex_io/sdk/audio_source/i_audio_source_client.h>

#include <json/json.h>

#include <memory>

namespace YandexIO {

    /**
     * @brief Class that simplifies BrokenMicDetector integration:
     *        Create and manage: BrokenMicLogger, BrokenMicConfigHandler, BrokenMicFirstConfigureHandler
     *        Code Example:
     *          BrokenMicDetectorSystemHolder brokenMicDetector(config);
     *          brokenMicDetector.init(sdk, audioSourceClient);
     *          sdk.init();
     *          brokenMicDetector.subscribeToRuntimeConfig(sdk);
     */
    class BrokenMicDetectorSystemHolder {
    public:
        explicit BrokenMicDetectorSystemHolder(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory, const Json::Value& config);
        /**
         * @brief Add IO SDK listeners and subscribe to audio data
         */
        void init(SDKInterface& sdk, IAudioSourceClient& audioSourceClient);

        /**
         * @brief Subscribe to runtime config via YandexIO SDK
         * @note: Should be called after YandexIOSDK::init
         */
        void subscribeToRuntimeConfig(SDKInterface& sdk);

    private:
        std::shared_ptr<BrokenMicLogger> detector_;
        std::shared_ptr<BrokenMicConfigHandler> runtimeConfigHandler_;
        std::shared_ptr<BrokenMicFirstConfigureHandler> firstConfigureHandler_;
    };

} /* namespace YandexIO */
