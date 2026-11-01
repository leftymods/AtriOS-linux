#pragma once

#include <span>

namespace telink {

    class ISwire {
    public:
        virtual ~ISwire();

        virtual uint32_t getClockRate() const = 0;

        virtual void write(uint8_t id, uint32_t addr, std::span<const uint8_t> data) = 0;
        virtual void read(uint8_t id, uint32_t addr, std::span<uint8_t> data) = 0;
    };

} // namespace telink
