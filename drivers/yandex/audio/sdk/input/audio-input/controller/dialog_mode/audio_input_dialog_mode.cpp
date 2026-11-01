#include "audio_input_dialog_mode.h"

namespace YandexIO {

    AudioInputDialogMode::AudioInputDialogMode(std::weak_ptr<IAudioInputController> controller)
        : controller_(std::move(controller))
    {
    }

    void AudioInputDialogMode::setASRMode() {
        if (auto scontroller = controller_.lock()) {
            scontroller->setASRMode();
        }
    }

    void AudioInputDialogMode::setSpotterMode() {
        if (auto scontroller = controller_.lock()) {
            scontroller->setSpotterMode();
        }
    }

    void AudioInputDialogMode::onAliceStateChanged(quasar::proto::AliceState state) {
        if (!state.has_state()) {
            return;
        }
        if (state.state() == state_) {
            return;
        }
        state_ = state.state();
        if (state_ == quasar::proto::AliceState::LISTENING) {
            /* Set up ASR mode only if Listening */
            setASRMode();
        } else {
            setSpotterMode();
        }
    }

    void AudioInputDialogMode::onAliceTtsCompleted() {
        // ¯\_(ツ)_/¯
    }

} /* namespace YandexIO */
