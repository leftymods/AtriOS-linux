#include "fake_uart.h"

#include <smart_devices/libs/telink/bootloader/uart.h>
#include <smart_devices/libs/telink/bootloader/xmodem.h>

#include <smart_devices/libs/tty/tty.h>

#include <library/cpp/testing/unittest/registar.h>

Y_UNIT_TEST_SUITE(TestXmodem) {

    static const std::string MESSAGE = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Mauris mattis leo mauris, ut venenatis enim sollicitudin ut. Nulla viverra scelerisque fermentum. Donec ut mauris erat. Donec ac convallis leo. Curabitur at leo quis urna euismod sollicitudin. Mauris congue luctus elementum. Nam sodales, sem at sagittis feugiat, quam metus lacinia sapien, quis porttitor libero mi sit amet ligula. Nunc viverra congue sapien, eget feugiat lectus ornare at. Vestibulum rhoncus volutpat magna, sit amet lacinia eros vestibulum ut. Donec neque metus, vulputate at imperdiet vel, euismod nec massa. Cras laoreet urna dignissim ante porta ultrices.";

    static bool xmodemHandler(void* arg, uint8_t* data, size_t size) {
        auto message = reinterpret_cast<std::string*>(arg);

        if (data == nullptr) {
            return true;
        }

        UNIT_ASSERT_EQUAL(size, XMODEM_DATA_SIZE);

        message->append((char*)data, size);

        return true;
    }

    Y_UNIT_TEST(testReceive) {
        tlUartInit(UART_BAUDRATE);

        quasar::Tty tty(getFakeUart().getPath(), quasar::Tty::Parameters());

        auto txThread = std::thread([&tty]() {
            auto success = tty.xmodemTransmit(MESSAGE);
            UNIT_ASSERT(success);
        });

        std::string message;

        auto error = tlXmodemReceive(xmodemHandler, &message);

        UNIT_ASSERT_EQUAL(error, XMODEM_OK);

        UNIT_ASSERT(message.size() - MESSAGE.size() < XMODEM_DATA_SIZE);
        UNIT_ASSERT(message.starts_with(MESSAGE));

        txThread.join();
    }

}
