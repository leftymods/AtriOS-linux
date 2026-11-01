#pragma once

#include <smart_devices/libs/tty/tty.h>

#include <string>

class GeckoBootloader {
public:
    using LogCallback = std::function<void(const std::string&)>;

    GeckoBootloader(const std::string& port);

    void resetGpio(bool bootloader, int resetPin, int bootloaderPin);

    bool isRunning();

    bool upload(const std::string& path, LogCallback cb);

    void run();

    void log(LogCallback cb);

private:
    const std::string port_;
    std::unique_ptr<quasar::Tty> tty_;
};
