#include "gpio.h"

#include <yandex_io/libs/errno/errno_exception.h>

#include <util/system/yassert.h>

#include <cassert>
#include <cerrno>
#include <stdexcept>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

using namespace quasar;

GPIO::GPIO(int number, Direction direction, Edge edge)
    : number_(number)
    , direction_(direction)
    , edge_(edge)
{
    FILE* unexportFile = fopen("/sys/class/gpio/unexport", "wt");
    if (nullptr == unexportFile) {
        throw ErrnoException(errno, "Cannot open /sys/class/gpio/unexport for writing");
    }
    fprintf(unexportFile, "%d", number);
    fclose(unexportFile);

    FILE* exportFile = fopen("/sys/class/gpio/export", "wt");
    if (nullptr == exportFile) {
        throw ErrnoException(errno, "Cannot open /sys/class/gpio/export for writing");
    }
    fprintf(exportFile, "%d", number);
    fclose(exportFile);

    const std::string baseDir = "/sys/class/gpio/gpio" + std::to_string(number_);
    std::string directionFileName = baseDir + "/direction";
    FILE* directionFile = fopen(directionFileName.c_str(), "wt");
    if (nullptr == directionFile) {
        throw ErrnoException(errno, "Cannot open " + directionFileName + " for writing");
    }
    switch (direction)
    {
        case Direction::IN:
            fprintf(directionFile, "in");
            break;
        case Direction::OUT:
            fprintf(directionFile, "out");
            break;
    }

    fclose(directionFile);

    if (Direction::IN == direction)
    {
        std::string edgeFileName = baseDir + "/edge";
        FILE* edgeFile = fopen(edgeFileName.c_str(), "wt");
        if (nullptr == edgeFile) {
            throw ErrnoException(errno, "Cannot open " + edgeFileName + " for writing");
        }
        fprintf(edgeFile, "%s", edgeToString(edge_).c_str());
        fclose(edgeFile);
    }
    std::string valueFileName = baseDir + "/value";
    valueFd_ = open(valueFileName.c_str(), (Direction::IN == direction) ? O_RDONLY : O_WRONLY);
    if (valueFd_ < 0) {
        throw ErrnoException(errno, "Cannot open " + valueFileName + ". Errno: " + std::to_string(errno));
    }
}

GPIO::GPIO(GPIO&& other) noexcept {
    if (valueFd_ > 0) {
        close(valueFd_);
    }

    number_ = other.number_;
    valueFd_ = other.valueFd_;
    direction_ = other.direction_;

    other.valueFd_ = -1;
}

GPIO& GPIO::operator=(GPIO&& other) noexcept {
    if (valueFd_ > 0) {
        close(valueFd_);
    }

    number_ = other.number_;
    valueFd_ = other.valueFd_;
    direction_ = other.direction_;

    other.valueFd_ = -1;

    return *this;
}

void GPIO::write(const std::string& value)
{
    Y_VERIFY(Direction::OUT == direction_);
    int res = ::write(valueFd_, value.c_str(), value.length());
    if (res < 0) {
        throw ErrnoException(errno, "Cannot write " + std::to_string(value.length()) +
                                        " bytes to value file. Errno: " + std::to_string(errno));
    }
}

int GPIO::readSync() const {
    struct pollfd pollfd[1];
    pollfd[0].fd = valueFd_;
    pollfd[0].events = POLLPRI | POLLERR;
    pollfd[0].revents = 0;

    int err = poll(pollfd, 1, -1);
    if (err < 0) {
        throw ErrnoException(errno, "poll failed");
    }
    char c = '\0';
    lseek(valueFd_, 0, SEEK_SET);
    int res = ::read(valueFd_, &c, 1);
    if (res < 0) {
        throw ErrnoException(errno, "Cannot read value");
    }

    return c - '0';
}

int GPIO::read() const {
    char c = '\0';
    lseek(valueFd_, 0, SEEK_SET);
    int res = ::read(valueFd_, &c, 1);

    if (res < 0) {
        throw ErrnoException(errno, "Cannot read value");
    }

    return c - '0';
}

int GPIO::getHandle() const {
    return valueFd_;
}

GPIO::~GPIO()
{
    if (valueFd_ > 0) {
        close(valueFd_);
    }
}

GPIO::Edge GPIO::getEdge() const {
    return edge_;
}

std::string GPIO::edgeToString(Edge edge) {
    switch (edge) {
        case Edge::FALLING:
            return "falling";
        case Edge::RISING:
            return "rising";
        default:
            return "both";
    }
}
