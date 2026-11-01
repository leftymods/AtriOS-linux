#include "b91.h"

using namespace telink;

namespace {

    constexpr uint32_t HCLK = 48'000'000;

    constexpr uint32_t REG_SWIRE_CTL2 = 0x00100c02;
    constexpr uint32_t REG_RST2 = 0x001401e2;

    constexpr size_t CHUNK_SIZE = 0x400;

} // namespace

B91::B91(std::shared_ptr<ISwire> swire)
    : swire_(std::move(swire))
{
}

bool B91::sync() {
    const uint8_t clkDiv = HCLK / swire_->getClockRate();
    swire_->write(0, REG_SWIRE_CTL2, {&clkDiv, 1});

    try {
        uint8_t reg;
        swire_->read(0, REG_SWIRE_CTL2, {&reg, 1});

        return reg == clkDiv;
    } catch (const std::runtime_error& e) {
        return false;
    }
}

void B91::reboot() {
    const uint8_t value = 0x20;
    swire_->write(0, 0x001401ef, {&value, 1});
}

void B91::runBinary(const std::string& bin) {
    const uint8_t halt = 0xe7;
    swire_->write(0, REG_RST2, {&halt, 1});

    const uint8_t zero = 0x00;
    swire_->write(0, 0x001401d5, {&zero, 1});
    swire_->write(0, 0x001401d6, {&zero, 1});
    swire_->write(0, 0x001401d7, {&zero, 1});

    for (size_t i = 0; i < bin.size(); i += CHUNK_SIZE) {
        std::span<const uint8_t> chunk{
            reinterpret_cast<const uint8_t*>(bin.data() + i),
            std::min(CHUNK_SIZE, bin.size() - i)};

        swire_->write(0, 0xc0000000 + i, chunk);
    }

    const uint8_t reset = 0xff;
    swire_->write(0, REG_RST2, {&reset, 1});
}
