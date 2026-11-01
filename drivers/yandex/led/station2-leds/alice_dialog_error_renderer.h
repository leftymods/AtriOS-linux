#pragma once

#include "led_devices_impl.h"

#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/sdk/sdk_state_observer.h>

#include <memory>

/*
 * @brief Pretty alice dialog error renderer
 */
class AliceDialogErrorRenderer final: public YandexIO::SDKStateObserver {
public:
    AliceDialogErrorRenderer(std::shared_ptr<quasar::LedManager> ledManager, std::shared_ptr<LedDevicesImpl> ledDevice);
    ~AliceDialogErrorRenderer() = default;

    /**
     * @brief Will call error animation if error happened during conversation
     */
    void onConversationError() override;

    void onSDKState(const YandexIO::SDKState& state) override;

private:
    std::shared_ptr<quasar::LedManager> ledManager_;
    std::shared_ptr<LedDevicesImpl> ledDevices_;
};
