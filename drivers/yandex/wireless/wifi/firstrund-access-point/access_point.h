#pragma once

#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_connector.h>

#include <string>

namespace quasar {

    class AccessPoint {
    public:
        virtual void start(const std::string& accessPointName) = 0;
        virtual void coldStart(const std::string& accessPointName);

        virtual void stop() = 0;
        virtual bool isStarted() const = 0;

        virtual ~AccessPoint(){};
    };

    class ScriptsAccessPoint: public AccessPoint {
    public:
        ScriptsAccessPoint(std::shared_ptr<YandexIO::IDevice> device, std::string startScriptPath, std::string stopScriptPath);
        void start(const std::string& accessPointName) override;
        void coldStart(const std::string& accessPointName) override;
        void stop() override;
        bool isStarted() const override;

    private:
        std::shared_ptr<YandexIO::IDevice> device_;

        std::string startScriptPath_;
        std::string stopScriptPath_;

        bool started_ = false;
    };

} // namespace quasar
