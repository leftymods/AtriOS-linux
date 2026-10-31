#pragma once

#include <yandex_io/modules/audio_input/controller/i_audio_input_controller.h>

#include <yandex_io/modules/audio_input/mics_state/i_mics_state_listener.h>

#include <memory>

namespace YandexIO {

    class AudioInputControllerMicsStateProxy: public IMicsStateListener {
    public:
        explicit AudioInputControllerMicsStateProxy(std::weak_ptr<IAudioInputController> controller);
        void onMuted() override;
        void onUnmuted() override;

    private:
        std::weak_ptr<IAudioInputController> controller_;
    };

} /* namespace YandexIO */
