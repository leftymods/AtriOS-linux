#pragma once

#include <util/system/file.h>

#include <optional>
#include <vector>

namespace quasar {

    class Tty {
    public:
        enum class BaudRate {
            BAUD_57600,
            BAUD_115200,
            BAUD_230400,
            BAUD_460800
        };

        enum class DataBits {
            D5,
            D6,
            D7,
            D8
        };

        enum class Parity {
            NONE,
            EVEN,
            ODD
        };

        enum class StopBits {
            ONE,
            TWO
        };

        enum class FlowControl {
            NONE,
            SOFT,
            HARD
        };

        struct Parameters {
            BaudRate baudRate = BaudRate::BAUD_115200;
            DataBits dataBits = DataBits::D8;
            Parity parity = Parity::NONE;
            StopBits stopBits = StopBits::ONE;
            FlowControl flowControl = FlowControl::NONE;
            uint8_t readTimeoutDsec = 10;
        };

        Tty(const std::string& port, const Parameters& parameters);

        std::optional<uint8_t> readByte();
        std::vector<uint8_t> read(size_t count);

        std::vector<uint8_t> readUntil(std::optional<uint8_t> until = std::nullopt);

        std::string readLine();

        void writeByte(uint8_t byte);
        size_t write(const std::vector<uint8_t>& data);

        bool xmodemTransmit(const std::string& data);

    private:
        void configureTty(const Parameters& parameters);

    private:
        TFileHandle fh_;
    };

} // namespace quasar
