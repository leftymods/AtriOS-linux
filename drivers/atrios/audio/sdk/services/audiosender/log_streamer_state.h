#pragma once

#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/extensions/permessage_deflate/enabled.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace AudioSender {

    class LogStreamerState {
    public:
        using SharedPtr = std::shared_ptr<LogStreamerState>;

        LogStreamerState();

        void onSetServerAddress(const std::string& address);
        void onStart();
        void onStop();
        void onWriteLog(const std::string& log);

    private:
        void ioRun();
        void connect();
        void reconnect();

    private:
        std::thread ioRunThread;
        struct WebSocketClientConfig: public websocketpp::config::asio_client {
            using permessage_deflate_type = websocketpp::extensions::permessage_deflate::enabled<websocketpp::config::asio_client::permessage_deflate_config>;
        };
        websocketpp::client<WebSocketClientConfig> client;
        websocketpp::connection_hdl handle;
        std::atomic_bool connected{false};
        std::atomic_bool launched{false};
        std::string serverAddress;
    };

} // namespace AudioSender
