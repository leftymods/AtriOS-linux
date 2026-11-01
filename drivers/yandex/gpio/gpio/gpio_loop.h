#pragma once

#include "gpio_input.h"

#include <atomic>
#include <chrono>
#include <map>
#include <optional>
#include <vector>

namespace quasar {

    class GPIOLoop {
    public:
        GPIOLoop(std::optional<std::chrono::milliseconds> maxPollTimeout);

        void addInput(GPIOInput* input);
        void start(std::atomic_bool& thread_exists, int wakeUpFd = -1);

    private:
        void checkInputs();
        void handleTimer();

        std::vector<GPIOInput*> inputs_;

        std::map<int, GPIOInput*> handleToInput_;
        std::map<int, const GPIO*> handleToGpio_;

        std::optional<std::chrono::milliseconds> maxPollTimeout_;

        GPIOLoop(const GPIOLoop& other) = delete;
        GPIOLoop& operator=(const GPIOLoop& other) = delete;
    };

} // namespace quasar
