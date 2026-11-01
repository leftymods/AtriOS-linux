#pragma once

#include <yandex_io/modules/audio_input/controller/i_audio_input_controller.h>
#include <yandex_io/capabilities/alice/interfaces/i_alice_capability_listener.h>

#include <memory>

namespace YandexIO {

    class AudioInputDialogMode: public IAliceCapabilityListener {
    public:
        explicit AudioInputDialogMode(std::weak_ptr<IAudioInputController> controller);
        void onAliceStateChanged(quasar::proto::AliceState state) override;
        void onAliceTtsCompleted() override;

    private:
        void setASRMode();
        void setSpotterMode();

        quasar::proto::AliceState::State state_{quasar::proto::AliceState::NONE};

        std::weak_ptr<IAudioInputController> controller_;
    };

} /* namespace YandexIO */
