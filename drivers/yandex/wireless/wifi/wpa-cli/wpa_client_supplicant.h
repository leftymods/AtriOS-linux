#pragma once

#include "wpa_client.h"

#include <yandex_io/libs/threading/steady_condition_variable.h>

#include <yandex_io/protos/quasar_proto.pb.h>

#include <atomic>
#include <future>

namespace quasar {

    class WpaClientSupplicant: public WpaClient {
    public:
        WpaClientSupplicant(const std::string& ifdir);
        ~WpaClientSupplicant();
        WpaClientSupplicant& operator=(const WpaClientSupplicant&) = delete;

        void setCallback(std::function<void(const char*)> fn) override;
        void clearCallback() override;

        void startThread() override;
        std::string scan() const override;
        std::string listNetworks() const override;
        std::string addNetwork() const override;
        std::string selectNetwork(int networkId) const override;
        std::string enableNetwork(int networkId) const override;
        std::string enableAllNetworks() const override;
        std::string disableNetwork(int networkId) const override;
        std::string disableAllNetworks() const override;
        std::string removeNetwork(int networkId) const override;
        std::string reloadConfig() const override;
        std::string saveConfig() const override;
        std::string status(char* arg) const override;
        std::string signalPoll() const override;
        std::string scanResults() const override;
        std::string setNetwork(int networkId, const char* varname, const char* value) const override;
        std::string getNetwork(int networkId, const char* varname) const override;
        std::string disconnect() const override;
        std::string reconnect() const override;
        std::string reassociate() const override;

        bool isAttached() const override;

    protected:
        const char* buffer() const override;
        class WpaGuard final {
        public:
            WpaGuard();
            ~WpaGuard();
        };
        void loopTask() override;
        std::thread eloop_;
        const char* ctrl_ifname_;
        const std::string ctrl_ifname_dir_;
    };

} // namespace quasar
