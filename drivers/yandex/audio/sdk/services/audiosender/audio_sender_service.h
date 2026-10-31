#pragma once

#include "audio_sender_endpoint.h"

#include <yandex_io/libs/base/quasar_service.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>

namespace quasar {

    class AudioSenderService: public QuasarService {
    public:
        AudioSenderService(
            std::shared_ptr<YandexIO::IDevice> device,
            const std::shared_ptr<ipc::IIpcFactory>& ipcFactory,
            SpeechKit::AudioPlayer::SharedPtr audioPlayer,
            std::shared_ptr<quasar::IAuthProvider> authProvider);

        std::string getServiceName() const override;
        void start() override;
        void stop() override;

    private:
        AudioSenderEndpoint endpoint_;
    };

} // namespace quasar
