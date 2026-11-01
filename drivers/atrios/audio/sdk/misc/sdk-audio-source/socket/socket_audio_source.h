#pragma once

#include <yandex_io/sdk/audio_source/defines.h>
#include <yandex_io/sdk/audio_source/base/audio_source.h>

#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/protos/yandex_io.pb.h>

#include <map>
#include <memory>

namespace YandexIO {
    /* AudioSource implementation that push data to Quasar via IPC server("yio_audio") */
    class SocketAudioSource: public AudioSource {
    public:
        explicit SocketAudioSource(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory);

    private:
        using Connection = quasar::ipc::IServer::IClientConnection;
        using SharedConnection = std::shared_ptr<Connection>;
        using FindIfLambda = std::function<bool(const YandexIO::ChannelData& channel)>;

    private:
        void pushDataImpl(ChannelsData data) override;

        void handleChannelRequest(const quasar::proto::IOChannelRequest& msg, Connection& connection);
        void handleClientDisconnected(Connection& connection);

        void sendChannel(const ChannelsData& data, RequestChannelType type, FindIfLambda lambda);
        void sendAll(const ChannelsData& data);
        void sendMessageToHandlers(const quasar::ipc::SharedMessage& msg, RequestChannelType type);

        void removeConnectionUnlocked(const SharedConnection& connectionToRemove);

    private:
        std::multimap<RequestChannelType, SharedConnection> connections_;
        std::mutex mutex_;
        std::shared_ptr<quasar::ipc::IServer> server_;
    };

} /* namespace YandexIO */
