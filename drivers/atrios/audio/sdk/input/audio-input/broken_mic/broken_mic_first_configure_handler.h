#pragma once

#include "broken_mic_logger.h"

#include <yandex_io/sdk/device_mode_observer.h>

#include <memory>
#include <optional>

namespace YandexIO {

    class BrokenMicFirstConfigureHandler: public DeviceModeObserver {
    public:
        explicit BrokenMicFirstConfigureHandler(std::shared_ptr<BrokenMicLogger> detector);
        void onStartSetup(bool isFirstSetup) override;
        void onFinishSetup() override;

        void onSoundDataTransferStart() override{};
        void onConnectingToNetwork() override{};
        void onSetupError() override{};
        void onConfigureSuccessUpdate(DeviceModeObserver::ConfigurationSuccessState /*configurationSuccessState*/) override{};

    private:
        std::shared_ptr<BrokenMicLogger> brokenMicDetector_;
        std::optional<bool> setupMode_ = std::nullopt;
    };

} /* namespace YandexIO */
