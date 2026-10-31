#pragma once

#include <array>
#include <span>

class FakeFlash {
public:
    FakeFlash();

    void init();

    void set(uint32_t addr, std::span<uint8_t> data);

    void read(uint32_t addr, std::span<uint8_t> data);

    void write(uint32_t addr, std::span<uint8_t> data);

    void erase(uint32_t addr);

public:
    static constexpr size_t SIZE = 0x200000;
    static constexpr size_t ERASE_SIZE = 0x1000;

private:
    std::array<uint8_t, SIZE> flash_;
};

FakeFlash& getFakeFlash();
