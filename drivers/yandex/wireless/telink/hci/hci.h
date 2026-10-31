#pragma once

#include <smart_devices/libs/tty/tty.h>

#include <span>
#include <thread>

namespace telink {

    enum class HciStatus: uint8_t {
        Success = 0x00,
        IncorrectParameters = 0x01,
        UnhandledCommand = 0x02,
        Busy = 0x03,
        NoMemory = 0x04,
        StackAlreadyStarted = 0x05,

        ErrorStartChar = 0xe0,
        MsgOverflow = 0xe1,
        ErrorEndChar = 0xe2,
        BadMsg = 0xe3,
        UartExcept = 0xe4,
        CrcError = 0xe5,
    };

    class HciListener {
    public:
        virtual ~HciListener();

        virtual HciStatus onCommand(uint16_t msgType, std::span<const uint8_t> payload) = 0;
    };

    class Hci {
    public:
        Hci(const std::string& port);
        ~Hci();

        void start(std::shared_ptr<HciListener> listener);
        void stop();

        HciStatus send(uint16_t msgType, std::span<const uint8_t> payload);

        template <class Command>
        HciStatus send(const Command& cmd) {
            return send(Command::TYPE, cmd.serialize());
        }

    private:
        void receiveLoop();

        void handleMessage(uint16_t msgType, std::span<const uint8_t> payload);

    private:
        const std::unique_ptr<quasar::Tty> tty_;

        std::shared_ptr<HciListener> listener_;

        std::atomic<bool> stopped_ = false;
        std::thread thread_;

        std::mutex sendMutex_;
        std::mutex ackMutex_;
        std::condition_variable ackCv_;
        std::optional<HciStatus> msgStatus_;
    };

} // namespace telink
