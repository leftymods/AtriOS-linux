#pragma once

#include "audio_source_adapter.h"
#include "command_server.h"
#include "discovery_agent.h"

#include <yandex_io/capabilities/device_state/host/device_state_capability_host.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/protos/quasar_proto_forward.h>

#include <yandex_io/sdk/audio_source/i_audio_source_client.h>
#include <yandex_io/sdk/private/endpoint_storage/endpoint_storage_host.h>
#include <yandex_io/sdk/private/remoting/remoting_message_router.h>

#include <memory>
#include <string>

namespace quasar {

    class AudioSenderEndpoint {
    public:
        static const std::string SERVICE_NAME;

        explicit AudioSenderEndpoint(
            std::shared_ptr<YandexIO::IDevice> device,
            const std::shared_ptr<ipc::IIpcFactory>& ipcFactory,
            SpeechKit::AudioPlayer::SharedPtr audioPlayer,
            std::shared_ptr<quasar::IAuthProvider> authProvider);
        void start();
        void stop();

    private:
        void onSyncMessage(const ipc::SharedMessage& message);
        void onMessageToAlice(const ipc::SharedMessage& message, ipc::IServer::IClientConnection& connection);

    private:
        std::shared_ptr<YandexIO::IDevice> device_;
        std::shared_ptr<AudioSender::AudioSourceAdapter> audioSourceAdapter_;
        std::shared_ptr<YandexIO::IAudioSourceClient> ioAudioSource_;
        std::shared_ptr<AudioSender::CommandServer> commandServer_;
        AudioSender::DiscoveryAgent discoveryAgent_;
        std::shared_ptr<ipc::IConnector> syncConnector_;
        std::shared_ptr<ipc::IServer> server_;
        std::shared_ptr<YandexIO::RemotingMessageRouter> remotingMessageRouter_;
        std::shared_ptr<YandexIO::DeviceStateCapabilityHost> deviceStateCapability_;
        std::shared_ptr<YandexIO::EndpointStorageHost> endpointStorage_;
    };

} // namespace quasar
