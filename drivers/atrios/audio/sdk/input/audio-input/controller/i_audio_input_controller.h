#pragma once

namespace YandexIO {

    class IAudioInputController {
    public:
        virtual ~IAudioInputController();

        virtual void setASRMode() = 0;
        virtual void setSpotterMode() = 0;

        virtual void startCommunicationMode();
        virtual void stopCommunicationMode();

        virtual void mute() = 0;
        virtual void unmute() = 0;
    };

} /* namespace YandexIO */
