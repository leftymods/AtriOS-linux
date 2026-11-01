#pragma once

#include "i_toggle_mics_state_provider.h"

namespace YandexIO {

    /**
     * @brief simple stub that do nothing on toggle and return false from "areMicsMuted" method
     */
    class StubToggleMicsStateProvider: public IToggleMicsStateProvider {
    public:
        void toggleMute() override;
        bool areMicsMuted() const override;
    };

} /* namespace YandexIO */
