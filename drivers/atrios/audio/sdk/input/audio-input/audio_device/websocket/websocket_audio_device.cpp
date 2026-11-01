#include "websocket_audio_device.h"

#include <yandex_io/libs/audio/common/defines.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <speechkit/SoundBuffer.h>
#include <speechkit/SoundFormat.h>
#include <speechkit/SoundInfo.h>
#include <speechkit/SpeechKit.h>
#include <speechkit/StreamDecoder.h>

#include <chrono>

YIO_DEFINE_LOG_MODULE("audio_device");

using namespace quasar;

namespace {

    const std::string DEFAULT_MAIN_CHANNEL = "vqe_0";
    constexpr int DEFAULT_SAMPLE_SIZE = 2;
    constexpr int DEFAULT_PORT = 8888;

    std::string getMainChannel(const Json::Value& config) {
        return ::quasar::tryGetString(config, "mainChannel", DEFAULT_MAIN_CHANNEL);
    }

    ::SpeechKit::SoundInfo getInputSoundInfo(const Json::Value& config) {
        using namespace SpeechKit;
        const int sampleSize = tryGetInt(config, "sampleSize", DEFAULT_SAMPLE_SIZE);
        const int sampleRate = tryGetInt(config, "sampleRate", DEFAULT_AUDIO_SAMPLING_RATE);
        return SoundInfo{SoundFormat::OPUS, 1, sampleRate, sampleSize};
    }

    ::YandexIO::AudioDevice::ChannelInfo getChannelInfo(const Json::Value& config) {
        ::YandexIO::AudioDevice::ChannelInfo info;
        info.sampleSize = tryGetUInt32(config, "sampleSize", DEFAULT_SAMPLE_SIZE);
        info.sampleRate = tryGetUInt32(config, "sampleRate", DEFAULT_AUDIO_SAMPLING_RATE);
        return info;
    }

    constexpr auto CHUNK_DURATION = std::chrono::milliseconds{100};

    void sleepForSound(std::chrono::milliseconds duration)
    {
        // Sleep for a bit less duration to prevent spaces in sound.
        std::this_thread::sleep_for(duration / 100 * 95);
    }

} // namespace

namespace YandexIO {

    WebsocketAudioDevice::WebsocketAudioDevice(const Json::Value& config, std::shared_ptr<YandexIO::ITelemetry> telemetry)
        : AudioDeviceBase{{{getMainChannel(config), getChannelInfo(config)}}, config}
        , telemetry_{std::move(telemetry)}
        , soundInfo_{getInputSoundInfo(config)}
        , channelName_{getMainChannel(config)}
    {
        YIO_LOG_INFO("Main channel: " << channelName_ << ", sound info: " << soundInfo_.toString());
        // We have to call SpeechKit::getInstance even if dumpPath is not specified to be able to create StreamDecoder.
        auto instance = ::SpeechKit::SpeechKit::getInstance();
        if (auto dumpPath{tryGetString(config, "dumpPath")}; !dumpPath.empty()) {
            instance->setDumpPath(dumpPath);
        }

        startServer(tryGetInt(config, "port", DEFAULT_PORT));
    }

    WebsocketAudioDevice::~WebsocketAudioDevice() {
        if (serverThread_.joinable()) {
            try {
                server_.stop();
            } catch (const std::exception& exception) {
                YIO_LOG_ERROR_EVENT("WebsocketAudioDevice.StopInDestructorFailed", "WebsocketServer can't stop in destructor: " << exception.what());
                telemetry_->reportKeyValues("WebsocketAudioDeviceException",
                                            {{"destructor_stop", exception.what()}});
            }
            serverThread_.join();
        }
    }

    void WebsocketAudioDevice::startServer(int port)
    {
        server_.set_access_channels(websocketpp::log::alevel::all);

        // dont log frame headers and bodies
        server_.clear_access_channels(websocketpp::log::alevel::frame_payload);
        server_.clear_access_channels(websocketpp::log::alevel::frame_header);

        server_.init_asio();

        server_.set_message_handler([this](websocketpp::connection_hdl handler, WebsocketAudioDevice::MessagePtr message) {
            onMessageHandler(std::move(handler), std::move(message));
        });
        server_.set_open_handler([this](websocketpp::connection_hdl handler) {
            onOpenHandler(std::move(handler));
        });
        server_.set_close_handler([this](websocketpp::connection_hdl handler) {
            onCloseHandler(std::move(handler));
        });
        server_.set_reuse_addr(true); // sets SO_REUSEADDR so we can restart with no delay

        YIO_LOG_INFO("Start listening on port " << port);
        try {
            server_.listen(port);
        } catch (const std::exception& exception) {
            handleFatalException(exception, "listen");
        }

        YIO_LOG_INFO("Run server");
        serverThread_ = std::thread([this] {
            runServer();
        });
    }

    void WebsocketAudioDevice::runServer() {
        try {
            server_.start_accept();
            server_.run();
        } catch (const std::exception& exception) {
            handleFatalException(exception, "start_accept_run");
        }
    }

    void WebsocketAudioDevice::onOpenHandler(websocketpp::connection_hdl connection)
    {
        if (!activeSoundStreaming_.load()) {
            YIO_LOG_DEBUG("new client connection");
            activeSoundStreaming_.store(true);
            activeConnection_ = connection;
            server_.send(connection, R"({"type": "connect", "result": "success"})",
                         websocketpp::frame::opcode::text);

            decoder_ = ::SpeechKit::StreamDecoder::create(soundInfo_, std::chrono::milliseconds{5000});
        } else {
            YIO_LOG_DEBUG("skip new client connection");
            server_.send(connection, R"({"type": "connect", "result": "failure", "reason": "occupied by another connection"})",
                         websocketpp::frame::opcode::text);
            server_.close(connection, websocketpp::close::status::try_again_later,
                          "occupied by another connection");
        }
    }

    void WebsocketAudioDevice::onCloseHandler(websocketpp::connection_hdl connection)
    {
        if (isActiveConnection(connection)) {
            YIO_LOG_DEBUG("close connection");
            activeSoundStreaming_.store(false);
            activeConnection_.reset();
            seqNo_ = 0;
        }
    }

    void WebsocketAudioDevice::onMessageHandler(websocketpp::connection_hdl connection, WebsocketAudioDevice::MessagePtr message)
    {
        if (!isActiveConnection(connection)) {
            YIO_LOG_WARN("Skip message without active connection");
            return;
        }

        if (message->get_opcode() == websocketpp::frame::opcode::binary) {
            const auto payload = message->get_payload();

            std::vector<uint8_t> data(reinterpret_cast<const std::uint8_t*>(payload.data()),
                                      reinterpret_cast<const std::uint8_t*>(payload.data() + payload.size()));
            ++seqNo_;

            {
                Json::Value ack;
                ack["type"] = "ack";
                ack["seqNo"] = seqNo_;
                ack["payloadSize"] = data.size();
                server_.send(connection, jsonToString(ack), websocketpp::frame::opcode::text);
            }

            std::scoped_lock lock{decoderMutex_};
            YIO_LOG_DEBUG("Push sound to decoder: " << data.size());
            decoder_->push(data);
        }
    }

    bool WebsocketAudioDevice::isActiveConnection(websocketpp::connection_hdl connection) const {
        return !activeConnection_.owner_before(connection) && !connection.owner_before(activeConnection_);
    }

    void WebsocketAudioDevice::doCapture()
    {
        std::unique_lock lock{decoderMutex_};
        const bool sendWebsocketSound = decoder_ != nullptr && !decoder_->isEmpty();
        lock.unlock();

        if (sendWebsocketSound) {
            pushWebsocketSound();
            return;
        }

        // Don't send empty sound if we have an active connection -> wait for real sound.
        const bool sendEmptySound = !activeSoundStreaming_.load();
        if (sendEmptySound) {
            pushEmptySound();
        }
    }

    void WebsocketAudioDevice::pushEmptySound() {
        YIO_LOG_TRACE("Push empty sound");
        // We need size in std::int16_t, not in std::uint8_t, so divide it by 2.
        static const auto zeroChunkDataSize =
            ::SpeechKit::SoundBuffer::calculateBufferSize(soundInfo_, CHUNK_DURATION) / 2;
        pushData({{channelName_, std::vector<std::int16_t>(zeroChunkDataSize)}});
        sleepForSound(CHUNK_DURATION);
    }

    void WebsocketAudioDevice::pushWebsocketSound() {
        std::unique_lock lock{decoderMutex_};
        if (!decoder_->tryDecode(CHUNK_DURATION)) {
            YIO_LOG_WARN("Can't decode sound!");
            return;
        }

        const auto decodedSound = decoder_->popDecodedSound();
        lock.unlock();

        std::size_t decodedSoundSize{};
        std::chrono::milliseconds decodedSoundDuration{};
        for (const auto& buffer : decodedSound) {
            decodedSoundSize += buffer->getData().size();
            decodedSoundDuration += std::chrono::milliseconds{buffer->calculateTimeMs()};
        }

        YIO_LOG_DEBUG("decodedSoundDuration: " << decodedSoundDuration.count());

        std::vector<std::int16_t> channelData;
        channelData.reserve(decodedSoundSize / 2);
        for (const auto& buffer : decodedSound) {
            const std::vector<std::uint8_t>& bufferData = buffer->getData();
            channelData.insert(channelData.end(),
                               reinterpret_cast<const std::int16_t*>(bufferData.data()),
                               reinterpret_cast<const std::int16_t*>(bufferData.data()) + bufferData.size() / 2);
        }

        if (!channelData.empty()) {
            YIO_LOG_DEBUG("Push websocket sound");
            pushData({{channelName_, std::move(channelData)}});
            sleepForSound(decodedSoundDuration);
        }
    }

    double WebsocketAudioDevice::getDOAAngle() const {
        return 0;
    }

    void WebsocketAudioDevice::handleFatalException(const std::exception& exception, const std::string& msg) {
        YIO_LOG_ERROR_EVENT("WebsocketAudioDevice.HandleFatalException", "WebsocketAudioDevice " << msg << ": " << exception.what());
        telemetry_->reportKeyValues("WebsocketAudioDeviceException", {{msg, exception.what()}});
        std::this_thread::sleep_for(std::chrono::seconds(5));
        throw exception;
    }

} // namespace YandexIO
