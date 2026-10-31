#pragma once

#include <yandex_io/libs/device/device.h>

#include <atomic>
#include <thread>

namespace AudioSender {

    class DiscoveryAgent {
    public:
        DiscoveryAgent(std::shared_ptr<YandexIO::IDevice> device);

        void start();
        void stop();

    private:
        void discovery();

    private:
        std::shared_ptr<YandexIO::IDevice> device_;

        std::atomic<bool> isRunning_;
        std::thread discoveryThread_;
        const std::string softwareVersion_;
    };

} // namespace AudioSender
