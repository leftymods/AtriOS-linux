#pragma once

#include <yandex_io/modules/audio_input/mics_state/i_mics_state_listener.h>

#include <functional>

namespace YandexIO {
    /**
     * @brief Simple IMicsState listener that forward new state to callback
     */
    class CallbackMicsStateListener: public IMicsStateListener {
    public:
        using OnMicsStateChanged = std::function<void(bool /* muted */)>;
        explicit CallbackMicsStateListener(OnMicsStateChanged onMicsStateChanged);

        void onMuted() override;
        void onUnmuted() override;

    private:
        OnMicsStateChanged onMicsStateChanged_;
    };

} /* namespace YandexIO */
