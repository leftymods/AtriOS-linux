#include "tcp_audio_device.h"

#include <yandex_io/libs/audio/common/defines.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <asio/ip/resolver_base.hpp>

#include <utility>

#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>

YIO_DEFINE_LOG_MODULE("audio_device");

using namespace quasar;
using tcp = asio::ip::tcp;

namespace YandexIO {
    namespace {
        const std::array<std::string, 9> CHANNELS = {
            "raw_mic_0",
            "raw_mic_1",
            "raw_mic_2",
            "raw_mic_3",
            "vqe_0",
            "vqe_bnr",
            "raw_sync_mic_main",
        };

        AudioDevice::ChannelsList getChannelsList() {
            AudioDevice::ChannelsList channels;
            const AudioDevice::ChannelInfo CHANNEL_INFO = {2u, 16000u};
            for (const auto& channel : CHANNELS) {
                channels[channel] = CHANNEL_INFO;
            }
            return channels;
        }

    } // namespace

    TCPAudioDevice::TCPAudioDevice(const Json::Value& config)
        : AudioDeviceBase(getChannelsList(), config)
        , periodSizeInFrames_(tryGetUInt64(config, "periodSize", DEFAULT_AUDIO_PERIOD_SIZE))
        , ioContext_()
        , acceptor_(ioContext_)
        , connectionSocket_(ioContext_)
    {
        for (const auto& channel : CHANNELS) {
            data_[channel] = std::vector<int16_t>(periodSizeInFrames_, 0);
        }

        const int port = getInt(config["audioDevice"], "port");
        const std::string host = tryGetString(config["audioDevice"], "bind", "localhost");

        tcp::resolver resolver(ioContext_);
        std::error_code ec;

        tcp::resolver::query resolver_query(tcp::v4(), host, std::to_string(port), tcp::resolver::query::passive);
        tcp::resolver::iterator it = resolver.resolve(resolver_query, ec);

        if (ec) {
            throw std::runtime_error("Error converting hostname to addres: " + ec.message());
        }

        acceptor_.open(it->endpoint().protocol(), ec);
        if (ec) {
            throw std::runtime_error("Error opening socket: " + ec.message());
        }

        acceptor_.non_blocking(true);
        acceptor_.set_option(asio::socket_base::reuse_address(true), ec);

        if (ec) {
            throw std::runtime_error("Error setting reuse_address to true: " + ec.message());
        }

        acceptor_.bind(it->endpoint(), ec);
        if (ec) {
            throw std::runtime_error("Error binding socket: " + ec.message());
        }

        acceptor_.listen(4, ec);

        if (ec) {
            throw std::runtime_error("Error calling listen: " + ec.message());
        }
        YIO_LOG_INFO("TCPAudioDevice started listening on " << it->endpoint())
    }

    TCPAudioDevice::~TCPAudioDevice() {
        AudioDeviceBase::stop();
    }

    double TCPAudioDevice::getDOAAngle() const {
        return 0;
    }

    void TCPAudioDevice::doCapture() {
        std::error_code ec;
        // This imitates the natural latency introduced by sampling the microphone, assuming close to instantaneous read from fifo
        std::this_thread::sleep_for(std::chrono::milliseconds((int)((double)periodSizeInFrames_ / DEFAULT_AUDIO_SAMPLING_RATE * 1000.0f)));

        auto buf = std::vector<int8_t>(periodSizeInFrames_ * FRAME_SIZE);

        if (!connectionSocket_.is_open()) {
            acceptor_.accept(connectionSocket_, ec);
            if (ec) {
                if (ec != asio::error::would_block && ec != asio::error::try_again) {
                    YIO_LOG_ERROR_EVENT("TCPAudioDevice.ErrorAcceptingConnection", "Error accepting connection: " + ec.message());
                }
            } else {
                YIO_LOG_INFO("Accepted connection from " << connectionSocket_.remote_endpoint().address().to_string());
            }
        }

        for (auto& [_, data] : data_) {
            data = std::vector<int16_t>(periodSizeInFrames_, 0);
        }

        if (connectionSocket_.is_open()) {
            const int size = asio::read(connectionSocket_, asio::buffer(buf, periodSizeInFrames_ * FRAME_SIZE), ec);

            if (size == -1 || size == 0) {
                if (ec && ec != asio::error::would_block && ec != asio::error::try_again) {
                    YIO_LOG_ERROR_EVENT("TCPAudioDevice.SocketReadFailed", "Error reading from socket: " + ec.message());
                }
                connectionSocket_.close();
            } else {
                if (size != static_cast<int>(periodSizeInFrames_ * FRAME_SIZE)) {
                    YIO_LOG_INFO("Read " << size << " bytes instead of " << periodSizeInFrames_ * FRAME_SIZE);
                }
                for (auto& [_, data] : data_) {
                    const auto* rawAudioSamples = reinterpret_cast<const int16_t*>(buf.data());
                    std::copy(rawAudioSamples, rawAudioSamples + size / sizeof(int16_t), data.begin());
                }
            }
        }
        AudioDeviceBase::pushData(data_);
    }
} // namespace YandexIO
