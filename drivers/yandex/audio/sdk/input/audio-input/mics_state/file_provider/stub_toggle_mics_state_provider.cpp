#include "stub_toggle_mics_state_provider.h"

namespace YandexIO {

    bool StubToggleMicsStateProvider::areMicsMuted() const {
        return false;
    }

    void StubToggleMicsStateProvider::toggleMute() {
        // no-op
    }

} /* namespace YandexIO */
