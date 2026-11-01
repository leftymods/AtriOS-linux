#include "gbl.h"

#include <smart_devices/libs/gpio/gpio.h>

#include <yandex_io/libs/base/utils.h>

#include <thread>

using namespace std::chrono_literals;
using namespace std::string_literals;

using namespace quasar;

GeckoBootloader::GeckoBootloader(const std::string& port)
    : port_(port)
    , tty_(std::make_unique<quasar::Tty>(port, quasar::Tty::Parameters()))
{
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void GeckoBootloader::resetGpio(bool bootloader, int resetPin, int bootloaderPin) {
    GPIO blGpio(bootloaderPin, GPIO::Direction::OUT);
    blGpio.write(bootloader ? "1" : "0");

    GPIO resetGpio(resetPin, GPIO::Direction::OUT);
    resetGpio.write("1");
    std::this_thread::sleep_for(100ms);

    resetGpio.write("0");
    std::this_thread::sleep_for(100ms);

    blGpio.write("0");
}

bool GeckoBootloader::isRunning() {
    tty_->readUntil();

    tty_->writeByte('\n');

    std::string line;

    do {
        line = tty_->readLine();

        if (line == "BL > \0"s) {
            return true;
        }
    } while (!line.empty());

    return false;
}

bool GeckoBootloader::upload(const std::string& path, LogCallback cb) {
    tty_->readUntil();

    tty_->writeByte('1');

    tty_->readLine();
    tty_->readLine();

    auto data = quasar::getFileContent(path);

    bool success = tty_->xmodemTransmit(data);

    log(cb);

    return success;
}

void GeckoBootloader::run() {
    tty_->writeByte('2');
}

void GeckoBootloader::log(LogCallback cb) {
    std::string line;

    do {
        line = tty_->readLine();

        if (line.starts_with("Gecko Bootloader")) {
            tty_->readUntil();
            return;
        }

        cb(trim(line));
    } while (!line.empty());
}
