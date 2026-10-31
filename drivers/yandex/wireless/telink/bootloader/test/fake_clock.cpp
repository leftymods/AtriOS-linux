#include <chrono>
#include <thread>

extern "C" {
    void WaitMs(uint32_t ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    uint32_t clock_time() {
        const auto now = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    }

    bool clock_time_exceed(uint32_t ref, uint32_t us) {
        return (clock_time() - ref) > us;
    }
}
