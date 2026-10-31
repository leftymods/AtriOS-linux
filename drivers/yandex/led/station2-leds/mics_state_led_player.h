#pragma once

#include "led_devices_impl.h"

#include <yandex_io/modules/audio_input/mics_state/i_mics_state_listener.h>
#include <yandex_io/modules/leds/led_manager/led_manager.h>

#include <memory>

/**
 * IMicsStateListener implementation that will draw Mics Mute animation
 */
class MicsStateLedPlayer: public YandexIO::IMicsStateListener {
public:
    MicsStateLedPlayer(std::shared_ptr<quasar::LedManager> ledManager, std::shared_ptr<LedDevicesImpl> ledDevices);

    void onMuted() override;
    void onUnmuted() override;

private:
    enum class MicsState {
        UNKNOWN,
        MUTED,
        UNMUTED
    };
    MicsState micsState_{MicsState::UNMUTED};
    std::shared_ptr<quasar::LedManager> ledManager_;
    std::shared_ptr<LedDevicesImpl> ledDevices_;
};
