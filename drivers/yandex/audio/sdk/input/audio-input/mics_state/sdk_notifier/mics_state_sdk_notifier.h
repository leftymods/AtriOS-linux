#pragma once

#include <yandex_io/modules/audio_input/mics_state/i_mics_state_listener.h>

#include <yandex_io/sdk/sdk_interface.h>
#include <yandex_io/capabilities/device_state/interfaces/i_device_state_capability.h>

#include <memory>

namespace YandexIO {

    /**
     * @brief Simple listener that notify SDK about mics state (used to setup ["device_state"]["mics_state"])
     */
    class MicsStateSDKNotifier: public IMicsStateListener {
    public:
        explicit MicsStateSDKNotifier(std::shared_ptr<SDKInterface> sdk);
        void onMuted() override;
        void onUnmuted() override;

    private:
        const std::shared_ptr<IDeviceStateCapability> dsc_;
    };

} /* namespace YandexIO */
