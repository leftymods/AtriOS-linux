#include "b91_flasher.h"

#include <yandex_io/libs/base/crc32.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <library/cpp/resource/resource.h>

using namespace telink;

namespace {

    constexpr std::string_view FLASHER_BIN = "dut_9518_flash_v0002.bin";

    constexpr size_t FLASH_PAGE_SIZE = 0x100;
    constexpr size_t FLASH_SECTOR_SIZE = 0x1000;
    constexpr size_t FLASH_READ_SIZE = 8;

    constexpr uint32_t SRAM_START = 0xc0000000;
    constexpr uint32_t REQUEST_ADDRESS = SRAM_START + 7;
    constexpr uint32_t RESPONSE_ADDRESS = SRAM_START + 4;
    constexpr uint32_t READ_ADDRESS = SRAM_START + 8;

    enum Command: uint8_t {
        CMD_BUFFER = 0x40,
        CMD_WRITE = 0x41,
        CMD_ERASE = 0x4d,
        CMD_READ = 0x4f,
        CMD_CRC32 = 0x50,
    };

    void init(B91& b91) {
        YIO_LOG_INFO("Loading flasher");

        const auto flasher = NResource::Find(FLASHER_BIN);

        bool syncOk = false;

        for (size_t i = 0; i < 3; i++) {
            b91.runBinary(flasher);

            if (b91.sync()) {
                syncOk = true;
                break;
            }
        }

        if (!syncOk) {
            throw std::runtime_error("Sync failed");
        }
    }

} // namespace

B91Flasher::B91Flasher(std::shared_ptr<ISwire> swire)
    : swire_(std::move(swire))
{
}

void B91Flasher::flash(uint32_t address, std::span<const uint8_t> data) {
    B91 b91(swire_);

    init(b91);

    YIO_LOG_INFO("Flashing " << quasar::Hex(data.size()) << " bytes at " << quasar::Hex(address));

    uint32_t bufferAddress = SRAM_START + getBufferOffset();
    YIO_LOG_DEBUG("Buffer address " << quasar::Hex(bufferAddress));

    quasar::Crc32 crc;

    for (uint32_t offset = 0; offset < data.size(); offset += FLASH_PAGE_SIZE) {
        if ((address + offset) % FLASH_SECTOR_SIZE == 0) {
            erase(address + offset);
        }

        auto chunk = data.subspan(offset, std::min(FLASH_PAGE_SIZE, data.size() - offset));

        write(address + offset, bufferAddress, chunk);

        crc.extend(reinterpret_cast<const char*>(chunk.data()), chunk.size());
    }

    uint32_t flashCrc32 = getCrc32(address, data.size());

    YIO_LOG_INFO("Flash CRC32 " << quasar::Hex(flashCrc32) << ", data CRC32 " << quasar::Hex(crc.checksum()));

    if (flashCrc32 != crc.checksum()) {
        throw std::runtime_error("CRC32 mismatch");
    }

    YIO_LOG_INFO("Rebooting");

    b91.reboot();
}

void B91Flasher::dump(uint32_t address, std::span<uint8_t> data) {
    B91 b91(swire_);

    init(b91);

    YIO_LOG_INFO("Dumping " << quasar::Hex(data.size()) << " bytes at " << quasar::Hex(address));

    quasar::Crc32 crc;

    for (uint32_t offset = 0; offset < data.size(); offset += FLASH_READ_SIZE) {
        auto chunk = data.subspan(offset, std::min(FLASH_READ_SIZE, data.size() - offset));

        read(address + offset, chunk);

        crc.extend(reinterpret_cast<const char*>(chunk.data()), chunk.size());
    }

    uint32_t flashCrc32 = getCrc32(address, data.size());

    YIO_LOG_INFO("Flash CRC32 " << quasar::Hex(flashCrc32) << ", data CRC32 " << quasar::Hex(crc.checksum()));

    YIO_LOG_INFO("Rebooting");

    b91.reboot();
}

uint16_t B91Flasher::getBufferOffset() {
    YIO_LOG_DEBUG("Obtaining buffer offset");

    uint32_t a = 0;
    uint32_t b = 0;

    return executeCommand(CMD_BUFFER, a, b);
}

void B91Flasher::write(uint32_t address, uint32_t bufferAddress, std::span<const uint8_t> data) {
    YIO_LOG_DEBUG("Writing " << quasar::Hex(data.size()) << " bytes to " << quasar::Hex(address));

    swire_->write(0x00, bufferAddress, data);

    uint32_t a = address;
    uint32_t b = data.size();

    executeCommand(CMD_WRITE, a, b);
}

void B91Flasher::erase(uint32_t address) {
    YIO_LOG_DEBUG("Erasing flash at " << quasar::Hex(address));

    uint32_t a = address;
    uint32_t b = 0x00000004;

    executeCommand(CMD_ERASE, a, b);
}

void B91Flasher::read(uint32_t address, std::span<uint8_t> data) {
    if (data.size() > FLASH_READ_SIZE) {
        throw std::invalid_argument("Bad read size");
    }

    YIO_LOG_DEBUG("Reading " << quasar::Hex(data.size()) << " bytes from " << quasar::Hex(address));

    uint32_t a = address;
    uint32_t b = data.size();

    executeCommand(CMD_READ, a, b);

    swire_->read(0x00, READ_ADDRESS, data);
}

uint32_t B91Flasher::getCrc32(uint32_t address, uint32_t count) {
    YIO_LOG_DEBUG("Getting CRC32 of " << quasar::Hex(count) << " bytes at " << quasar::Hex(address));

    uint32_t a = address;
    uint32_t b = count;

    executeCommand(CMD_CRC32, a, b);

    return b;
}

uint16_t B91Flasher::executeCommand(uint8_t command, uint32_t& a, uint32_t& b) {
    std::array request = {
        command,
        static_cast<uint8_t>(a & 0xff),
        static_cast<uint8_t>((a >> 8) & 0xff),
        static_cast<uint8_t>((a >> 16) & 0xff),
        static_cast<uint8_t>((a >> 24) & 0xff),
        static_cast<uint8_t>(b & 0xff),
        static_cast<uint8_t>((b >> 8) & 0xff),
        static_cast<uint8_t>((b >> 16) & 0xff),
        static_cast<uint8_t>((b >> 24) & 0xff),
    };

    swire_->write(0x00, REQUEST_ADDRESS, request);

    uint8_t status = command | 0x80;
    swire_->write(0x00, REQUEST_ADDRESS, {&status, 1});

    for (size_t i = 0; i < 1000; i++) {
        swire_->read(0x00, REQUEST_ADDRESS, {&status, 1});

        if (status == command) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (status != command) {
        throw std::runtime_error("Command timeout");
    }

    std::array<uint8_t, 12> response;
    swire_->read(0x00, RESPONSE_ADDRESS, response);

    YIO_LOG_DEBUG("Command response " << quasar::Hex(response));

    if (response[2] != command || response[3] != command) {
        throw std::runtime_error("Bad response");
    }

    a = response[4] | (response[5] << 8) | (response[6] << 16) | (response[7] << 24);
    b = response[8] | (response[9] << 8) | (response[10] << 16) | (response[11] << 24);

    return response[0] | (response[1] << 8);
}
