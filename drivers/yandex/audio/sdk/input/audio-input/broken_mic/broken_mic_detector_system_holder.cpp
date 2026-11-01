#include "broken_mic_detector_system_holder.h"

namespace YandexIO {

    BrokenMicDetectorSystemHolder::BrokenMicDetectorSystemHolder(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory, const Json::Value& config) {
        detector_ = std::make_shared<YandexIO::BrokenMicLogger>(ipcFactory, config);
        firstConfigureHandler_ = std::make_shared<YandexIO::BrokenMicFirstConfigureHandler>(detector_);
        runtimeConfigHandler_ = std::make_shared<YandexIO::BrokenMicConfigHandler>(detector_);
    }

    void BrokenMicDetectorSystemHolder::init(SDKInterface& sdk, IAudioSourceClient& audioSourceClient) {
        sdk.addDeviceModeObserver(firstConfigureHandler_);
        sdk.addBackendConfigObserver(runtimeConfigHandler_);
        audioSourceClient.addListener(detector_);
    }

    void BrokenMicDetectorSystemHolder::subscribeToRuntimeConfig(SDKInterface& sdk) {
        runtimeConfigHandler_->subscribeToConfig(sdk);
    }

} /* namespace YandexIO */
