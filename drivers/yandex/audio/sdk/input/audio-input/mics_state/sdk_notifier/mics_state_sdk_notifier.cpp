#include "mics_state_sdk_notifier.h"

namespace YandexIO {

    MicsStateSDKNotifier::MicsStateSDKNotifier(std::shared_ptr<SDKInterface> sdk)
        : dsc_(sdk->getDeviceStateCapability())
    {
    }

    void MicsStateSDKNotifier::onMuted() {
        dsc_->setMicsMuted(true);
    }

    void MicsStateSDKNotifier::onUnmuted() {
        dsc_->setMicsMuted(false);
    }

} /* namespace YandexIO */
