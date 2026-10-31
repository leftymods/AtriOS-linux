#include "i_audio_source_client.h"

#include "socket/socket_audio_source_client.h"

namespace YandexIO {
    IAudioSourceClient::Listener::~Listener() = default;

    IAudioSourceClient::~IAudioSourceClient() = default;

    std::shared_ptr<IAudioSourceClient> createAudioSourceClient(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory) {
        return std::make_shared<SocketAudioSourceClient>(ipcFactory);
    }

} /* namespace YandexIO */
