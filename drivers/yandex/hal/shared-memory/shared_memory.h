#pragma once

#include <yandex_io/libs/errno/errno_exception.h>

#include <utility>

#include <sys/mman.h>

/**
 * Shared memory RAII wrapper with unique ownership semantics.
 */
template <typename T>
class SharedMemory {
public:
    using value_type = T;

    inline constexpr explicit SharedMemory() noexcept
        : ptr_{nullptr}
        , size_{0}
    {
    }

    SharedMemory(void* addr, std::size_t len, int prot, int flags, int fd, off64_t offset)
        : size_(len)
    {
        ptr_ = static_cast<value_type*>(mmap(addr, len, prot, flags, fd, offset));
        if (ptr_ == MAP_FAILED) {
            throw quasar::ErrnoException{errno, std::string("cannot mmap framebuffer: ")};
        }
    }

    SharedMemory(const SharedMemory& other) = delete;
    SharedMemory& operator=(const SharedMemory& other) = delete;

    inline constexpr SharedMemory(SharedMemory&& other) noexcept {
        *this = std::move(other);
    }

    inline SharedMemory& operator=(SharedMemory&& other) noexcept {
        if (ptr_ != nullptr) {
            munmap(ptr_, size_);
        }

        ptr_ = other.ptr_;
        size_ = other.size_;

        other.ptr_ = nullptr;
        other.size_ = 0;

        return *this;
    }

    ~SharedMemory() noexcept {
        if (ptr_ != nullptr) {
            munmap(ptr_, size_);
        }
    }

    [[nodiscard]] inline constexpr value_type* get() const noexcept {
        return ptr_;
    }

private:
    value_type* ptr_{};
    std::size_t size_{};
};
