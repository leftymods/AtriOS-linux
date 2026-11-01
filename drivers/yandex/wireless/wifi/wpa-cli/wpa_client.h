#pragma once

#include <yandex_io/protos/quasar_proto.pb.h>

#include <functional>
#include <thread>

namespace quasar {

    class WpaClient {
    public:
        virtual ~WpaClient() = default;
        virtual void setCallback(std::function<void(const char*)> fn) = 0;
        virtual void clearCallback() = 0;

        virtual void startThread() = 0;
        virtual std::string scan() const = 0;
        virtual std::string listNetworks() const = 0;
        virtual std::string addNetwork() const = 0;
        virtual std::string selectNetwork(int networkId) const = 0;
        virtual std::string enableNetwork(int networkId) const = 0;
        virtual std::string enableAllNetworks() const = 0;
        virtual std::string disableNetwork(int networkId) const = 0;
        virtual std::string disableAllNetworks() const = 0;
        virtual std::string removeNetwork(int networkId) const = 0;
        virtual std::string reloadConfig() const = 0;
        virtual std::string saveConfig() const = 0;
        virtual std::string status(char* arg) const = 0;
        virtual std::string signalPoll() const = 0;
        virtual std::string scanResults() const = 0;
        virtual std::string setNetwork(int networkId, const char* varname, const char* value) const = 0;
        virtual std::string getNetwork(int networkId, const char* varname) const = 0;
        virtual std::string disconnect() const = 0;
        virtual std::string reconnect() const = 0;
        virtual std::string reassociate() const = 0;

        virtual void loopTask() = 0;

        virtual bool isAttached() const = 0;

    protected:
        virtual const char* buffer() const = 0;
    };

    class WpaClientException: public std::runtime_error {
    public:
        explicit WpaClientException(const char* msg)
            : std::runtime_error(msg) {
        }
        explicit WpaClientException(const std::string& msg)
            : std::runtime_error(msg) {
        }
    };

} // namespace quasar
