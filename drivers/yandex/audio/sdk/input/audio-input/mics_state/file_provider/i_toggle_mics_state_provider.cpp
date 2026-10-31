#include "i_toggle_mics_state_provider.h"

namespace YandexIO {

    void IToggleMicsStateProvider::notifyCurrentState() {
        if (areMicsMuted()) {
            notifyMuted();
        } else {
            notifyUnmuted();
        }
    }

} /* namespace YandexIO */
