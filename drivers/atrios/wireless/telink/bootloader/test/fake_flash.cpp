#include "fake_flash.h"

#include <util/generic/singleton.h>

#include <algorithm>

extern "C" {
    void flash_write(uint32_t addr, uint32_t len, uint8_t* buf) {
        getFakeFlash().write(addr, {buf, len});
    }

    void flash_read(uint32_t addr, uint32_t len, uint8_t* buf) {
        getFakeFlash().read(addr, {buf, len});
    }

    void flash_erase(uint32_t addr) {
        getFakeFlash().erase(addr);
    }
}

FakeFlash::FakeFlash() {
    init();
}

void FakeFlash::init() {
    std::fill(flash_.begin(), flash_.end(), 0);
}

void FakeFlash::set(uint32_t addr, std::span<uint8_t> data) {
    if (addr >= flash_.size() || (addr + data.size()) > flash_.size()) {
        throw std::out_of_range("write");
    }

    for (size_t i = 0; i < data.size(); i++) {
        flash_[addr + i] = data[i];
    }
}

void FakeFlash::read(uint32_t addr, std::span<uint8_t> data) {
    if (addr >= flash_.size() || (addr + data.size()) > flash_.size()) {
        throw std::out_of_range("read");
    }

    for (size_t i = 0; i < data.size(); i++) {
        data[i] = flash_[addr + i];
    }
}

void FakeFlash::write(uint32_t addr, std::span<uint8_t> data) {
    if (addr >= flash_.size() || (addr + data.size()) > flash_.size()) {
        throw std::out_of_range("write");
    }

    for (size_t i = 0; i < data.size(); i++) {
        flash_[addr + i] &= data[i];

        if (flash_[addr + i] != data[i]) {
            throw std::runtime_error("write without erase");
        }
    }
}

void FakeFlash::erase(uint32_t addr) {
    if (addr >= flash_.size()) {
        throw std::out_of_range("erase");
    }

    uint32_t start = addr & ~(ERASE_SIZE - 1);

    for (size_t i = 0; i < ERASE_SIZE; i++) {
        flash_[start + i] = 0xff;
    }
}

FakeFlash& getFakeFlash() {
    return *HugeSingleton<FakeFlash>();
}
