#pragma once

#include "command_result.h"

#include <yandex_io/interfaces/auth/i_auth_provider.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/threading/i_callback_queue.h>
#include <yandex_io/libs/threading/unique_callback.h>
#include <yandex_io/protos/model_objects.pb.h>
#include <yandex_io/sdk/private/device_context.h>

#include <speechkit/UniProxyClient.h>

#include <atomic>
#include <future>
#include <memory>

namespace AudioSender {

    class YandexMusicPlayer: public SpeechKit::UniProxyClient::UniProxyClientListener, public std::enable_shared_from_this<YandexMusicPlayer> {
    public:
        using SharedPtr = std::shared_ptr<YandexMusicPlayer>;
        static SharedPtr create(
            const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory,
            const std::string& uuid,
            const std::string& apiKey,
            const std::string& uniProxyUrl,
            std::shared_ptr<quasar::IAuthProvider> authProvider);

        explicit YandexMusicPlayer(
            const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory,
            const std::string& uniProxyUrl,
            std::shared_ptr<quasar::IAuthProvider> authProvider);
        ~YandexMusicPlayer();

        CommandResult play(const std::string& music, int offset);
        CommandResult stop();
        void onMusicState(const quasar::proto::AppState::MusicState& musicState);

        /* from SpeechKit::UniProxyClient::UniProxyClientListener */
        void onUniProxyProtocolDirective(SpeechKit::UniProxyClient::SharedPtr uniProxyClient,
                                         const std::string& jsonDirective) override;
        void onConnectionStateChanged(SpeechKit::UniProxyClient::SharedPtr uniProxyClient, bool isConnected) override;
        void onUniProxyProtocolError(SpeechKit::UniProxyClient::SharedPtr uniProxyClient,
                                     const SpeechKit::Error& error) override;
        void onUniProxyProtocolStreamBegin(SpeechKit::UniProxyClient::SharedPtr /*uniProxyClient*/,
                                           const SpeechKit::UniProxy::DataStream& /*stream*/) override {
        }
        void onUniProxyProtocolStreamData(SpeechKit::UniProxyClient::SharedPtr /*uniProxyClient*/,
                                          const SpeechKit::UniProxy::DataStream& /*stream*/,
                                          const SpeechKit::UniProxy::StreamData& /*data*/) override {
        }
        void onUniProxyProtocolStreamEnd(SpeechKit::UniProxyClient::SharedPtr /*uniProxyClient*/,
                                         const SpeechKit::UniProxy::DataStream& /*stream*/) override {
        }

    private:
        void playInternal(const std::string& music, int offset);
        void stopInternal();
        void startFromOauthInit();
        void startFromUniproxyInit();
        void initMusicSessionId();
        void sendPlayToMedia();
        void setTimeoutCallback(const std::chrono::seconds& timeout, const std::string& error);

    private:
        const std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory_;
        const std::string uniProxyUrl_;
        std::shared_ptr<quasar::IAuthProvider> authProvider_;
        std::shared_ptr<quasar::ICallbackQueue> callbackQueue_;
        quasar::Lifetime lifetime_;
        quasar::UniqueCallback uniqueCallback_;
        std::shared_ptr<quasar::ipc::IConnector> mediaConnector_;

        enum class State {
            IDLE,
            STARTING_FROM_OAUTH,
            STARTING_FROM_UNIPROXY,
            SESSION_ID_INITIALIZING,
            WAITING_FIRST_HEARTBIT,
            PLAYING,
            STOPPING
        };
        State state_ = State::IDLE;

        std::shared_ptr<std::promise<void>> promise_;
        std::string music_;
        int offset_;
        std::string oauthToken_;
        SpeechKit::UniProxyClient::SharedPtr uniProxy_;
        bool uniproxyConnected_{false};
        std::string musicSessionId_;
    };

} // namespace AudioSender
