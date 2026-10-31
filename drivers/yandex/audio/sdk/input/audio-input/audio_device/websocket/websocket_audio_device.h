#pragma once

#include <yandex_io/modules/audio_input/audio_device/base/audio_device_base.h>

#include <yandex_io/libs/telemetry/telemetry.h>
#include <yandex_io/libs/websocket/quasar_ws_logger.h>

#include <json/json.h>

#include <speechkit/SoundInfo.h>
#include <speechkit/StreamDecoder.h>

#include <websocketpp/server.hpp>
#include <websocketpp/config/asio_no_tls.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace websocketpp {
    namespace config {

        // Server config with disabled TLS and custom logger
        struct webaudio: public asio {
            using type = webaudio;
            using asio = base;

            using concurrency_type = base::concurrency_type;

            using request_type = base::request_type;
            using response_type = base::response_type;

            using message_type = base::message_type;
            using con_msg_manager_type = base::con_msg_manager_type;
            using endpoint_msg_manager_type = base::endpoint_msg_manager_type;

            using alog_type = QuasarWsLogger<base::concurrency_type, log::alevel>;
            using elog_type = QuasarWsLogger<base::concurrency_type, log::elevel>;

            using rng_type = base::rng_type;

            struct transport_config: public base::transport_config {
                using concurrency_type = type::concurrency_type;
                using alog_type = type::alog_type;
                using elog_type = type::elog_type;
                using request_type = type::request_type;
                using response_type = type::response_type;
                using socket_type = websocketpp::transport::asio::basic_socket::endpoint;
            };

            using transport_type = websocketpp::transport::asio::endpoint<transport_config>;
        };

    } // namespace config
} // namespace websocketpp

namespace YandexIO {

    class WebsocketAudioDevice final: public AudioDeviceBase {
    public:
        WebsocketAudioDevice(const Json::Value& config, std::shared_ptr<YandexIO::ITelemetry> telemetry);
        ~WebsocketAudioDevice();

    private:
        using MessagePtr = typename websocketpp::config::webaudio::message_type::ptr;

        void startServer(int port);
        void runServer();

        void onOpenHandler(websocketpp::connection_hdl connection);
        void onCloseHandler(websocketpp::connection_hdl connection);
        void onMessageHandler(websocketpp::connection_hdl connection, MessagePtr message);

        double getDOAAngle() const override;
        void doCapture() override;

        bool isActiveConnection(websocketpp::connection_hdl connection) const;

        void pushEmptySound();
        void pushWebsocketSound();

        void handleFatalException(const std::exception& exception, const std::string& msg);

    private:
        const std::shared_ptr<YandexIO::ITelemetry> telemetry_;
        websocketpp::server<websocketpp::config::webaudio> server_;
        std::thread serverThread_;

        std::mutex decoderMutex_;
        std::shared_ptr<::SpeechKit::StreamDecoder> decoder_;

        const ::SpeechKit::SoundInfo soundInfo_;
        const std::string channelName_;
        std::atomic<bool> activeSoundStreaming_{false};

        std::int64_t seqNo_ = 0;

        websocketpp::connection_hdl activeConnection_;
    };

} // namespace YandexIO
