#include "tty.h"

#include <yandex_io/libs/errno/errno_exception.h>

#include <smart_devices/third_party/xmodem/xmodem.h>

#include <fcntl.h>
#include <sys/ttydefaults.h>
#include <termios.h>
#include <unistd.h>

using namespace quasar;

Tty::Tty(const std::string& port, const Parameters& parameters)
    : fh_{open(port.c_str(), O_RDWR | O_CLOEXEC)}
{
    if (!fh_.IsOpen()) {
        throw ErrnoException(errno, "Cannot open port '" + port + "'");
    }

    configureTty(parameters);
}

void Tty::configureTty(const Parameters& parameters) {
    struct termios tty;

    if (tcgetattr(fh_, &tty) != 0) {
        throw ErrnoException(errno, "tcgetattr failed");
    }

    cfmakeraw(&tty);

    tty.c_cflag |= (CREAD | CLOCAL);

    switch (parameters.parity) {
        case Parity::NONE:
            tty.c_cflag &= ~PARENB;
            break;
        case Parity::EVEN:
            tty.c_cflag |= PARENB;
            tty.c_cflag &= ~PARODD;
            break;
        case Parity::ODD:
            tty.c_cflag |= PARENB;
            tty.c_cflag |= PARODD;
            break;
    }

    switch (parameters.stopBits) {
        case StopBits::ONE:
            tty.c_cflag &= ~CSTOPB;
            break;
        case StopBits::TWO:
            tty.c_cflag |= CSTOPB;
            break;
    }

    tty.c_cflag &= ~CSIZE;

    switch (parameters.dataBits) {
        case DataBits::D5:
            tty.c_cflag |= CS5;
            break;
        case DataBits::D6:
            tty.c_cflag |= CS6;
            break;
        case DataBits::D7:
            tty.c_cflag |= CS7;
            break;
        case DataBits::D8:
            tty.c_cflag |= CS8;
            break;
    }

    if (parameters.flowControl == FlowControl::HARD) {
        tty.c_cflag |= CRTSCTS;
    } else {
        tty.c_cflag &= ~CRTSCTS;
    }

    if (parameters.flowControl == FlowControl::SOFT) {
        tty.c_iflag |= IXON | IXOFF;
    } else {
        tty.c_iflag &= ~(IXON | IXOFF);
    }

    memset(tty.c_cc, _POSIX_VDISABLE, NCCS);

    tty.c_cc[VSTART] = CSTART;
    tty.c_cc[VSTOP] = CSTOP;

    tty.c_cc[VTIME] = parameters.readTimeoutDsec;
    tty.c_cc[VMIN] = 0;

    speed_t speed;

    switch (parameters.baudRate) {
        case BaudRate::BAUD_57600:
            speed = B57600;
            break;
        case BaudRate::BAUD_115200:
            speed = B115200;
            break;
        case BaudRate::BAUD_230400:
            speed = B230400;
            break;
        case BaudRate::BAUD_460800:
            speed = B460800;
            break;
    }

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    if (tcsetattr(fh_, TCSANOW, &tty) != 0) {
        throw ErrnoException(errno, "tcsetattr failed");
    }
}

std::optional<uint8_t> Tty::readByte() {
    uint8_t byte;

    auto ret = fh_.Read(&byte, sizeof(byte));

    if (ret < 0) {
        throw ErrnoException(errno, "read failed");
    }

    if (ret != sizeof(byte)) {
        return std::nullopt;
    }

    return byte;
}

std::vector<uint8_t> Tty::read(size_t count) {
    std::vector<uint8_t> data(count);

    auto it = data.begin();

    while (it != data.end()) {
        auto ret = fh_.Read(&*it, data.end() - it);

        if (ret < 0) {
            throw ErrnoException(errno, "read failed");
        }

        if (ret == 0) {
            break;
        }

        it += ret;
    }

    data.resize(it - data.begin());

    return data;
}

std::vector<uint8_t> Tty::readUntil(std::optional<uint8_t> until) {
    std::vector<uint8_t> buffer;

    while (true) {
        auto byte = readByte();
        if (!byte) {
            break;
        }

        buffer.push_back(*byte);

        if (until && *byte == *until) {
            break;
        }
    }

    return buffer;
}

std::string Tty::readLine() {
    auto buffer = readUntil('\n');

    return std::string{buffer.begin(), buffer.end()};
}

void Tty::writeByte(uint8_t byte) {
    auto ret = fh_.Write(&byte, sizeof(byte));

    if (ret < 0) {
        throw ErrnoException(errno, "write failed");
    }

    Y_ASSERT(ret == sizeof(byte)); // should never happen
}

size_t Tty::write(const std::vector<uint8_t>& data) {
    auto ret = fh_.Write(data.data(), data.size());

    if (ret < 0) {
        throw ErrnoException(errno, "write failed");
    }

    return ret;
}

bool Tty::xmodemTransmit(const std::string& data) {
    auto ret = ::xmodemTransmit(fh_, reinterpret_cast<const unsigned char*>(data.data()), data.size());

    return ret >= 0;
}
