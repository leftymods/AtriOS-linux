#include "fake_flash.h"
#include "fake_uart.h"

#include <smart_devices/libs/telink/bootloader/bootloader.h>
#include <smart_devices/libs/telink/bootloader/uart.h>

#include <smart_devices/libs/gecko_bootloader/gbl.h>
#include <smart_devices/libs/tty/tty.h>

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <library/cpp/testing/common/env.h>
#include <library/cpp/testing/unittest/registar.h>

using namespace std::string_literals;

namespace {

    std::array<uint8_t, 4> TELINK_MAGIC = {'K', 'N', 'L', 'T'};

    class BootException: public std::exception {
    };

    void boot() {
        throw BootException();
    }

    struct BootloaderFixture: NUnitTest::TBaseFixture {
        using Base = NUnitTest::TBaseFixture;

        void SetUp(NUnitTest::TTestContext& context) override {
            Base::SetUp(context);

            getFakeFlash().init();
            getFakeFlash().set(FLASH_TLNK_FLAG_OFFSET, TELINK_MAGIC);

            tlUartInit(UART_BAUDRATE);

            ttyPath_ = getFakeUart().getPath();

            booted_ = false;
            stop_ = false;

            thread_ = std::thread([this]() {
                try {
                    tlBootloaderMain(false, boot, &stop_);
                } catch (const BootException& e) {
                    YIO_LOG_INFO("BOOT");
                    booted_ = true;
                }
            });
        }

        void TearDown(NUnitTest::TTestContext& context) override {
            Base::TearDown(context);

            stop_ = true;

            thread_.join();
        }

        const std::string& getTtyPath() {
            return ttyPath_;
        }

        bool hasBooted() const {
            return booted_;
        }

    private:
        std::string ttyPath_;

        bool stop_;
        std::thread thread_;

        bool booted_;
    };

} // namespace

Y_UNIT_TEST_SUITE(TestBootloader) {
    Y_UNIT_TEST_F(testMenu, BootloaderFixture) {
        quasar::Tty tty(getTtyPath(), quasar::Tty::Parameters());

        std::string line;

        UNIT_ASSERT_EQUAL(tty.readLine(), "\r\n");
        UNIT_ASSERT_EQUAL(tty.readLine(), "Yandex Bootloader v1.0.01\r\n");
        UNIT_ASSERT_EQUAL(tty.readLine(), "1. upload ybl\r\n");
        UNIT_ASSERT_EQUAL(tty.readLine(), "2. run\r\n");
        UNIT_ASSERT_EQUAL(tty.readLine(), "BL > \0"s);
        UNIT_ASSERT(tty.readLine().empty());
    }

    Y_UNIT_TEST_F(testUpload, BootloaderFixture) {
        GeckoBootloader gbl(getTtyPath());

        UNIT_ASSERT(gbl.isRunning());

        auto path = ArcadiaSourceRoot() + "/smart_devices/libs/telink/bootloader/test/data/sample.ybl";

        bool success = gbl.upload(path, [](const std::string& message) {
            YIO_LOG_INFO("UPLOAD: " << message);
        });

        UNIT_ASSERT(success);

        auto firmware = quasar::getFileContent(path);

        std::string ota(firmware.size(), 0);
        std::span otaData{reinterpret_cast<uint8_t*>(ota.data()), ota.size()};

        getFakeFlash().read(BOOTLOADER_OTA_ADDRESS, otaData);
        UNIT_ASSERT_EQUAL(firmware, ota);

        UNIT_ASSERT(!hasBooted());
        UNIT_ASSERT(gbl.isRunning());

        gbl.run();
        gbl.log([](const std::string& message) {
            YIO_LOG_INFO("RUN: " << message);
        });

        UNIT_ASSERT(hasBooted());
        UNIT_ASSERT(!gbl.isRunning());

        getFakeFlash().read(BOOTLOADER_APP_ADDRESS, otaData);
        UNIT_ASSERT_EQUAL(firmware, ota);

        getFakeFlash().read(BOOTLOADER_OTA_ADDRESS, otaData);
        UNIT_ASSERT_UNEQUAL(firmware, ota);
    }

    Y_UNIT_TEST(testBoot) {
        getFakeFlash().init();
        getFakeFlash().set(FLASH_TLNK_FLAG_OFFSET, TELINK_MAGIC);
        getFakeFlash().set(BOOTLOADER_APP_ADDRESS + FLASH_TLNK_FLAG_OFFSET, TELINK_MAGIC);

        UNIT_ASSERT_EXCEPTION(tlBootloaderMain(true, boot, nullptr), BootException);
    }
}
