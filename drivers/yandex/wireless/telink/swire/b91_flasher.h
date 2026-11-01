#pragma once

#include "b91.h"

namespace telink {

    class B91Flasher {
    public:
        B91Flasher(std::shared_ptr<ISwire> swire);

        void flash(uint32_t address, std::span<const uint8_t> data);

        void dump(uint32_t address, std::span<uint8_t> data);

    private:
        uint16_t executeCommand(uint8_t command, uint32_t& a, uint32_t& b);

        uint16_t getBufferOffset();

        void write(uint32_t address, uint32_t bufferAddress, std::span<const uint8_t> data);

        void erase(uint32_t address);

        void read(uint32_t address, std::span<uint8_t> data);

        uint32_t getCrc32(uint32_t address, uint32_t count);

    private:
        std::shared_ptr<ISwire> swire_;
    };

} // namespace telink
