#pragma once

#include <contrib/libs/libev/ev++.h>

#include <util/system/file.h>

#include <span>
#include <string>
#include <thread>

class FakeUart {
public:
    FakeUart();
    ~FakeUart();

    void init(std::span<uint8_t> rxBuffer, std::function<void()> rxCallback);

    void write(std::span<uint8_t> data);

    const std::string& getPath();

private:
    void stop();
    void loop();

    void readCallback();
    void stopCallback();

private:
    std::unique_ptr<TFileHandle> master_;
    std::unique_ptr<TFileHandle> slave_;
    std::string path_;

    std::span<uint8_t> rxBuffer_;
    std::function<void()> rxCallback_;

    std::thread thread_;
    ev::dynamic_loop loop_;
    ev::io readWatcher_;
    ev::async stopWatcher_;
};

FakeUart& getFakeUart();
