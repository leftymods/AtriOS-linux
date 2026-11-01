#include "log_streamer_state.h"

#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("audio_sender");

using namespace AudioSender;

namespace {

    const char* const LOG_SENDING_PORT = ":8788";

} // namespace

LogStreamerState::LogStreamerState() {
    client.clear_access_channels(websocketpp::log::alevel::all);
    client.clear_error_channels(websocketpp::log::elevel::all);

    client.init_asio();
    client.start_perpetual();

    ioRunThread = std::thread(&LogStreamerState::ioRun, this);
}

void LogStreamerState::onSetServerAddress(const std::string& address) {
    if (serverAddress != address) {
        bool expected = true;
        if (launched.compare_exchange_strong(expected, false)) {
            onStop();
        }
        serverAddress = address;
    }
}

void LogStreamerState::onStart() {
    bool expected = false;
    if (launched.compare_exchange_strong(expected, true)) {
        connect();
    }
}

void LogStreamerState::onStop() {
    websocketpp::lib::error_code ignoredError;
    client.pause_reading(handle, ignoredError);
    client.close(handle, websocketpp::close::status::normal, "", ignoredError);
    launched.store(false);
}

void LogStreamerState::onWriteLog(const std::string& log) {
    if (connected.load() && launched.load()) {
        websocketpp::lib::error_code ec;
        client.send(handle, log, websocketpp::frame::opcode::text, ec);
        if (ec) {
            YIO_LOG_ERROR_EVENT("LogStreamer.SendingError", ec.message());
        }
    }
}

void LogStreamerState::ioRun() {
    try {
        client.run();
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("LogStreamer.ClientRunException", e.what());
    } catch (const websocketpp::lib::error_code& e) {
        YIO_LOG_ERROR_EVENT("LogStreamer.ClientRunWebsocketppException", e.message());
    } catch (...) {
        YIO_LOG_ERROR_EVENT("LogStreamer.UnknownException", "");
    }
}

void LogStreamerState::connect() {
    websocketpp::lib::error_code ignoredError;
    client.pause_reading(handle, ignoredError);
    client.close(handle, websocketpp::close::status::normal, "", ignoredError);

    websocketpp::lib::error_code ec;
    auto connection = client.get_connection("ws://" + serverAddress + LOG_SENDING_PORT, ec);
    if (ec) {
        YIO_LOG_ERROR_EVENT("LogStreamer.GetConnectionError", ec.message());
        return;
    }

    connection->set_open_handler([this](websocketpp::connection_hdl /* connectionHandler */) {
        YIO_LOG_INFO("Connection to WS server for log sending established");
        connected.store(true);
    });
    connection->set_fail_handler([this](websocketpp::connection_hdl connectionHandler) {
        YIO_LOG_ERROR_EVENT("LogStreamer.ConnectionFailed", client.get_con_from_hdl(connectionHandler)->get_ec().message());
        reconnect();
    });
    connection->set_close_handler([this](websocketpp::connection_hdl /*hdl*/) {
        YIO_LOG_DEBUG("LogStreamer: Connection closed");
        reconnect();
    });
    connection->set_interrupt_handler([this](websocketpp::connection_hdl /*hdl*/) {
        YIO_LOG_DEBUG("LogStreamer: Connection interrupted");
        reconnect();
    });

    ec = client.connect(connection)->get_ec();
    if (ec) {
        YIO_LOG_ERROR_EVENT("LogStreamer.ClientConnectFailed", ec.message());
        return;
    }
    handle = connection->get_handle();
}

void LogStreamerState::reconnect() {
    connected.store(false);
    if (launched.load()) {
        connect();
    }
}
