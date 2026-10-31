#pragma once

#include <yandex_io/modules/audio_input/mics_state/mics_state_provider.h>

#include <atomic>
#include <string>

namespace YandexIO {

    class IToggleMicsStateProvider: public MicsStateProvider {
    public:
        virtual void toggleMute() = 0;
        virtual bool areMicsMuted() const = 0;

        /**
         * @brief Provide listeners current mics state
         */
        void notifyCurrentState();
    };

} /* namespace YandexIO */
