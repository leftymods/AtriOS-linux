#include "i_audio_input_controller.h"

#include <stdexcept>

namespace YandexIO {

    IAudioInputController::~IAudioInputController() = default;

    void IAudioInputController::startCommunicationMode() {
        throw std::runtime_error("Not implemented");
    }

    void IAudioInputController::stopCommunicationMode() {
        throw std::runtime_error("Not implemented");
    }

} /* namespace YandexIO */
