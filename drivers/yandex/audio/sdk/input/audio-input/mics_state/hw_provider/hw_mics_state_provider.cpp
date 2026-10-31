#include "hw_mics_state_provider.h"

namespace YandexIO {

    void HwMicsStateProvider::mute() {
        notifyMuted();
    }

    void HwMicsStateProvider::unmute() {
        notifyUnmuted();
    }

} /* namespace YandexIO */
