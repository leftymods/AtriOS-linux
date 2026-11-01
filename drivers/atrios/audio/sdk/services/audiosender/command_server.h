#pragma once

#include "audio_config.h"
#include "audio_source_adapter.h"
#include "command_result.h"
#include "log_sending_resul_sink.h"
#include "phrase_spotter_wrapper.h"
#include "sound_collector.h"
#include "sound_player.h"
#include "stream_sound_collector.h"
#include "yandex_music_player.h"

#include <yandex_io/sdk/interfaces/i_capability.h>
#include <yandex_io/sdk/interfaces/i_endpoint_storage.h>

#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>

#include <yandex_io/sdk/interfaces/i_endpoint_storage.h>

#include <speechkit/EventLogger.h>
#include <speechkit/SoundLogger.h>

#include <json/json.h>

#include <websocketpp/server.hpp>
#include <websocketpp/config/asio_no_tls.hpp>

#include <future>
#include <memory>
#include <mutex>
#include <thread>

namespace AudioSender {

    using EndpointSharedPtr = std::shared_ptr<websocketpp::server<websocketpp::config::asio>>;

    class MetricaEventLogger: public SpeechKit::EventLogger {
    public:
        MetricaEventLogger(std::shared_ptr<YandexIO::ITelemetry> telemetry);
        void reportEvent(const std::string& eventName, const EventArgs& args) override;

    private:
        std::shared_ptr<YandexIO::ITelemetry> telemetry_;
    };

    class CommandServer
        : public YandexIO::ICapability::IListener {
    public:
        explicit CommandServer(
            const Json::Value& config,
            std::shared_ptr<YandexIO::IDevice> device,
            std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory,
            std::weak_ptr<YandexIO::IEndpointStorage> endpointStorage,
            AudioSourceAdapter::SharedPtr adapter,
            SpeechKit::AudioPlayer::SharedPtr audioPlayer,
            const std::string& runtimeConfigPath,
            std::shared_ptr<quasar::IAuthProvider> authProvider);
        ~CommandServer();
        void start();
        void stop();

        void onMusicState(const quasar::proto::AppState::MusicState& musicState);

    private:
        void command_handler(websocketpp::connection_hdl handler,
                             websocketpp::server<websocketpp::config::asio>::message_ptr message);
        static Json::Value processPrepare();
        Json::Value processRecord(const Json::Value& request,
                                  websocketpp::connection_hdl handler);
        Json::Value processStopRecording(const Json::Value& request);
        Json::Value processPlay(const Json::Value& request,
                                websocketpp::connection_hdl handler);
        Json::Value processStopPlaying(const Json::Value& request);
        Json::Value processSend(const Json::Value& request,
                                websocketpp::connection_hdl handler);
        Json::Value processAutoSend(const std::string& recordId);
        Json::Value processManualSend(const std::string& recordId,
                                      websocketpp::connection_hdl handler);
        Json::Value processPlayMusic(const Json::Value& request);
        Json::Value processPlayFile(const Json::Value& request);
        Json::Value processStopMusic();
        Json::Value processGetVolume();
        Json::Value processSetVolume(const Json::Value& request);
        Json::Value processGetRecordedSound(const Json::Value& request, websocketpp::connection_hdl handler);
        Json::Value processGetAudioConfig();
        Json::Value processSubscribeLogs(const Json::Value& request,
                                         websocketpp::connection_hdl handler);
        static Json::Value processUnsubscribeLogs();
        Json::Value processGetConfig();
        Json::Value processSetConfig(const Json::Value& request);
        Json::Value processStartStreaming(const Json::Value& request, websocketpp::connection_hdl handler);
        Json::Value processStopStreaming();

        void processBinaryMessage(websocketpp::connection_hdl /*handler*/, websocketpp::server<websocketpp::config::asio>::message_ptr /*message*/);
        void processRestart(websocketpp::connection_hdl handler);

        CommandResult setDumpingChannels(const std::unordered_set<std::string>& dumpingChannels);
        void endpointSend(websocketpp::connection_hdl hdl, const Json::Value& payload);

        // ICapability::IListener
        void onCapabilityStateChanged(const std::shared_ptr<YandexIO::ICapability>& capability, const NAlice::TCapabilityHolder& state) override;
        void onCapabilityEvents(const std::shared_ptr<YandexIO::ICapability>& capability, const std::vector<NAlice::TCapabilityEvent>& events) override;

        CommandResult setVolume(int aliceVolume);

    private:
        enum class State {
            IDLE,
            MANUAL_SEND,
            SPOTTER_MANUAL_SEND,
            SPOTTER_AUTO_SEND,
            STREAMING
        };

        struct SoundUploadRequest {
            bool isActive;
            size_t expectedDataSize;
            std::string refMessageId;
            std::vector<uint8_t> rawData;
            SpeechKit::SoundInfo soundInfo;
        };

    private:
        Json::Value config;
        std::string runtimeConfigPath;
        std::shared_ptr<quasar::IAuthProvider> authProvider_;
        const std::shared_ptr<YandexIO::IDevice> device;
        const std::weak_ptr<YandexIO::IEndpointStorage> endpointStorage_;
        std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory;
        AudioConfig::SharedPtr audioConfig;
        std::thread asioServiceThread;
        EndpointSharedPtr endpoint{std::make_shared<websocketpp::server<websocketpp::config::asio>>()};
        SoundPlayer::SharedPtr soundPlayer;
        SoundCollector::SharedPtr soundCollector;
        StreamSoundCollector::SharedPtr streamSoundCollector;
        PhraseSpotterWrapper::SharedPtr phraseSpotter;
        Json::Value soundLogPayload;
        std::promise<void> sendingExpectation;
        LogSendingResultSink::SharedPtr logSendingResultSink;
        AudioSourceAdapter::SharedPtr quasarAudioSourceAdapter;
        std::chrono::seconds sendLogTimeout;

        std::once_flag musicPlayerFlag;
        YandexMusicPlayer::SharedPtr musicPlayer;

        SoundUploadRequest soundUploadRequest;
        State state;

        const std::string uniProxyUrl;

        std::shared_ptr<SpeechKit::EventLogger> eventLogger;

        std::string rebootCommand;

        std::mutex volumeMutex_;
        int volume_{0};
        quasar::SteadyConditionVariable volumeAcknowledgeCV_;
    };

} // namespace AudioSender
