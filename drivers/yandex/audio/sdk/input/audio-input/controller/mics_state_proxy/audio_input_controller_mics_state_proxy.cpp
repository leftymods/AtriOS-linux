#include "audio_input_controller_mics_state_proxy.h"

namespace YandexIO {

    AudioInputControllerMicsStateProxy::AudioInputControllerMicsStateProxy(std::weak_ptr<IAudioInputController> controller)
        : controller_(std::move(controller))
    {
    }

    void AudioInputControllerMicsStateProxy::onMuted() {
        if (auto controller = controller_.lock()) {
            controller->mute();
        }
    }

    void AudioInputControllerMicsStateProxy::onUnmuted() {
        if (auto controller = controller_.lock()) {
            controller->unmute();
        }
    }

} /* namespace YandexIO */
