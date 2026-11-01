#include "fake_uart.h"
#include "tl_common.h"

#include <yandex_io/libs/logging/logging.h>

#include <util/generic/singleton.h>
#include <util/system/compiler.h>

#include <pty.h>

extern "C" {
    void drv_uart_init(uint32_t baudRate, uint8_t* rxBuf, uint16_t rxBufLen, void (*uartRecvCb)()) {
        Y_UNUSED(baudRate);

        getFakeUart().init({rxBuf, rxBufLen}, uartRecvCb);
    }

    uint8_t drv_uart_tx_start(uint8_t* data, uint32_t len) {
        getFakeUart().write({data, len});
        return 1;
    }
}

FakeUart::FakeUart()
    : readWatcher_(loop_)
    , stopWatcher_(loop_)
{
    readWatcher_.set<FakeUart, &FakeUart::readCallback>(this);
    stopWatcher_.set<FakeUart, &FakeUart::stopCallback>(this);
}

FakeUart::~FakeUart() {
    stop();
}

void FakeUart::init(std::span<uint8_t> rxBuffer, std::function<void()> rxCallback) {
    stop();

    int masterFd;
    int slaveFd;
    char name[256];

    int ret = openpty(&masterFd, &slaveFd, name, nullptr, nullptr);
    Y_VERIFY(ret == 0);

    master_ = std::make_unique<TFileHandle>(masterFd);
    slave_ = std::make_unique<TFileHandle>(slaveFd);
    path_ = name;

    rxBuffer_ = rxBuffer;
    rxCallback_ = rxCallback;

    readWatcher_.start(*master_, ev::READ);
    stopWatcher_.start();

    thread_ = std::thread(&FakeUart::loop, this);

    YIO_LOG_INFO("UART started: " << path_);
}

void FakeUart::stop() {
    stopWatcher_.send();

    if (thread_.joinable()) {
        thread_.join();
    }

    readWatcher_.stop();
    stopWatcher_.stop();
}

void FakeUart::write(std::span<uint8_t> data) {
    master_->Write(data.data(), data.size());
}

const std::string& FakeUart::getPath() {
    return path_;
}

void FakeUart::loop() {
    loop_.run();
}

void FakeUart::readCallback() {
    uint32_t length;

    auto ret = master_->Read(rxBuffer_.data() + sizeof(length), rxBuffer_.size() - sizeof(length));

    if (ret > 0) {
        length = ret;

        memcpy(rxBuffer_.data(), &length, sizeof(length));

        rxCallback_();
    }
}

void FakeUart::stopCallback() {
    loop_.break_loop();
}

FakeUart& getFakeUart() {
    return *Singleton<FakeUart>();
}
