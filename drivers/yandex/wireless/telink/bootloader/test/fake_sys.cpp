#include <stdexcept>

extern "C" {
    void sys_reboot() {
        throw std::runtime_error("REBOOT");
    }
}
