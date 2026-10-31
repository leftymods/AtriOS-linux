#include "broken_mic_first_configure_handler.h"

namespace YandexIO {

    BrokenMicFirstConfigureHandler::BrokenMicFirstConfigureHandler(std::shared_ptr<BrokenMicLogger> detector)
        : brokenMicDetector_(std::move(detector))
    {
    }

    void BrokenMicFirstConfigureHandler::onStartSetup(bool isFirstSetup) {
        if (setupMode_.value_or(false)) {
            return;
        }
        setupMode_ = true;

        if (isFirstSetup) {
            brokenMicDetector_->startForcedRun();
        }
    }

    void BrokenMicFirstConfigureHandler::onFinishSetup() {
        setupMode_ = false;
        brokenMicDetector_->stopForcedRun();
    }

} /* namespace YandexIO */
