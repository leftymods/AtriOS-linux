#pragma once

#include <yandex_io/sdk/audio_source/i_audio_source_client.h>

#include <yandex_io/libs/ipc/i_ipc_factory.h>

#include <list>
#include <mutex>
#include <optional>

namespace YandexIO {
    /* Client for YandexIO::AudioSource.
     *
     * This particular implementation receive all channels from SocketAudioSource and
     * forward it to Listeners.
     */
    class SocketAudioSourceClient: public IAudioSourceClient {
    public:
        SocketAudioSourceClient(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory);
        ~SocketAudioSourceClient();

        /* Starts connection to AudioSource */
        void start() override;

        void subscribeToChannels(RequestChannelType type) override;

        void unsubscribeFromChannels() override;

        void addListener(std::weak_ptr<Listener> listener) override;

    private:
        void sendRequestChannel(RequestChannelType type);
        std::mutex typeMutex_;
        std::optional<RequestChannelType> requestedType_;
        std::list<std::weak_ptr<Listener>> listeners_;
        std::shared_ptr<quasar::ipc::IConnector> connector_;
    };

} /* namespace YandexIO */
