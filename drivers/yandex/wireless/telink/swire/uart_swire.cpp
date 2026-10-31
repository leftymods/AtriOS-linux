#include "uart_swire.h"

using namespace telink;

namespace {

    std::vector<uint8_t> encode(const std::vector<uint16_t>& cmd) {
        std::vector<uint8_t> bytes;

        bytes.reserve(cmd.size() * 5);

        for (uint16_t c : cmd) {
            for (int i = 0; i < 5; i++) {
                uint8_t byte = 0x00;

                if ((c & 0x100) != 0) {
                    byte |= 0x08;
                } else {
                    byte |= 0x0f;
                }

                if ((c & 0x80) == 0) {
                    byte |= 0xe0;
                }

                c <<= 2;

                bytes.push_back(byte);
            }
        }

        return bytes;
    }

    uint8_t decode(const std::vector<uint8_t>& bytes) {
        uint8_t result = 0x00;

        if (bytes.size() != 6 || bytes[0] != 0x00) {
            throw std::runtime_error("Decode error");
        }

        for (size_t i = 1; i < 5; i++) {
            result <<= 2;

            if ((bytes[i] & 0x0f) == 0x08) {
                result |= 0x02;
            }

            if ((bytes[i] & 0xf0) == 0x00) {
                result |= 0x01;
            }
        }

        return result;
    }

} // namespace

UartSwire::UartSwire(const std::string& port) {
    quasar::Tty::Parameters parameters = {
        .baudRate = quasar::Tty::BaudRate::BAUD_460800,
    };

    tty_ = std::make_unique<quasar::Tty>(port, parameters);
    tty_->readUntil();
}

uint32_t UartSwire::getClockRate() const {
    return 460800;
}

void UartSwire::write(uint8_t id, uint32_t addr, std::span<const uint8_t> data) {
    std::vector<uint16_t> cmd = {
        0x1ff,
        0x15a,
        static_cast<uint16_t>((addr >> 24) & 0xff),
        static_cast<uint16_t>((addr >> 16) & 0xff),
        static_cast<uint16_t>((addr >> 8) & 0xff),
        static_cast<uint16_t>(addr & 0xff),
        static_cast<uint16_t>(id & 0x7f),
    };

    cmd.insert(cmd.end(), data.begin(), data.end());
    cmd.push_back(0x1ff);

    transmit(cmd);
}

void UartSwire::read(uint8_t id, uint32_t addr, std::span<uint8_t> data) {
    std::vector<uint16_t> cmd = {
        0x1ff,
        0x15a,
        static_cast<uint16_t>((addr >> 24) & 0xff),
        static_cast<uint16_t>((addr >> 16) & 0xff),
        static_cast<uint16_t>((addr >> 8) & 0xff),
        static_cast<uint16_t>(addr & 0xff),
        static_cast<uint16_t>(id | 0x80),
    };

    transmit(cmd);

    for (size_t i = 0; i < data.size(); i++) {
        tty_->writeByte(0x00);

        auto bytes = tty_->read(6);

        if (bytes.size() != 6) {
            throw std::runtime_error("Transmit error");
        }

        data[i] = decode(bytes);
    }

    transmit({0x1ff});
}

void UartSwire::transmit(const std::vector<uint16_t>& data) {
    auto bytes = encode(data);

    size_t count = tty_->write(bytes);

    if (count != bytes.size()) {
        throw std::runtime_error("Transmit error");
    }

    auto echo = tty_->read(count);

    if (echo.size() != bytes.size()) {
        throw std::runtime_error("Transmit error");
    }
}
