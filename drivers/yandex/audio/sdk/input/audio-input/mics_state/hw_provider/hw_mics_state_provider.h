#pragma once

#include <yandex_io/modules/audio_input/mics_state/mics_state_provider.h>

namespace YandexIO {

    class HwMicsStateProvider: public MicsStateProvider {
    public:
        void mute();
        void unmute();
    };

} /* namespace YandexIO */
