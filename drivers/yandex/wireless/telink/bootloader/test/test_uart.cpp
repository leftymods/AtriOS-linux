#include "fake_uart.h"

#include <smart_devices/libs/telink/bootloader/uart.h>

#include <smart_devices/libs/tty/tty.h>

#include <library/cpp/testing/unittest/registar.h>

#include <util/string/hex.h>

Y_UNIT_TEST_SUITE(TestUart) {
    const std::array<uint8_t, 6> MESSAGE{'U', 'A', 'R', 'T', '\r', '\n'};

    Y_UNIT_TEST(testSend) {
        tlUartInit(UART_BAUDRATE);

        quasar::Tty tty(getFakeUart().getPath(), quasar::Tty::Parameters());

        tlUartSend(MESSAGE.data(), MESSAGE.size());

        UNIT_ASSERT_EQUAL(tty.readLine(), "UART\r\n");
    }

    Y_UNIT_TEST(testReceive) {
        tlUartInit(UART_BAUDRATE);

        quasar::Tty tty(getFakeUart().getPath(), quasar::Tty::Parameters());

        UNIT_ASSERT_EQUAL(tlUartAvailableBytes(), 0);

        for (uint8_t byte : MESSAGE) {
            tty.writeByte(byte);
        }

        std::array<uint8_t, 6> buffer;

        const auto time = clock_time();

        while (!tlUartReceive(buffer.data(), buffer.size())) {
            if (clock_time_exceed(time, 1000 * 1000)) {
                UNIT_FAIL("timeout");
            }
        }

        UNIT_ASSERT_EQUAL(buffer, MESSAGE);
        UNIT_ASSERT_EQUAL(tlUartAvailableBytes(), 0);
    }

}
