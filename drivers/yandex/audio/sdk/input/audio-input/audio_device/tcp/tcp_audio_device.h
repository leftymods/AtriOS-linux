#pragma once

#include <yandex_io/modules/audio_input/audio_device/base/audio_device_base.h>

#include <json/json.h>

#include <asio.hpp>

namespace YandexIO {

    class TCPAudioDevice: public AudioDeviceBase {
    public:
        explicit TCPAudioDevice(const Json::Value& config);
        ~TCPAudioDevice() override;
        double getDOAAngle() const override;

    private:
        void doCapture() override;

    private:
        const size_t periodSizeInFrames_;
        std::mutex mutex_;
        asio::io_context ioContext_;
        asio::ip::tcp::acceptor acceptor_;
        asio::ip::tcp::socket connectionSocket_;

        ChannelToData data_;

        constexpr static size_t CHANNEL_COUNT{1};

        constexpr static size_t FRAME_SIZE = sizeof(int16_t) * CHANNEL_COUNT;
    };

} // namespace YandexIO
