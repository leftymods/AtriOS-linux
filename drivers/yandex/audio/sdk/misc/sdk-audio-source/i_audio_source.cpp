#include "i_audio_source.h"

#include "socket/socket_audio_source.h"

#include <yandex_io/libs/ipc/i_ipc_factory.h>

namespace YandexIO {
    IAudioSource::~IAudioSource() = default;

    std::shared_ptr<IAudioSource> createAudioSource(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory) {
        return std::make_shared<SocketAudioSource>(ipcFactory);
    }

} /* namespace YandexIO */
