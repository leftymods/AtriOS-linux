#pragma once

#include <yandex_io/services/wifid/wifi_endpoint.h>
#include <yandex_io/services/wifid/wifi_manager.h>
#include <yandex_io/services/wifid/wifi_utils.h>

#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <queue>
#include <unordered_map>

namespace quasar {

    class TestWpaClient: public WpaClient {
    public:
        enum CmdType {
            SCAN = 0,
            LIST_NETWORK,
            ADD_NETWORK,
            SELECT_NETWORK,
            ENABLE_NETWORK,
            ENABLE_ALL_NETWORKS,
            DISABLE_NETWORK,
            DISABLE_ALL_NETWORKS,
            REMOVE_NETWORK,
            RECONFIGURE,
            DISCONNECT,
            RECONNECT,
            REASSOCIATE,
            SAVE,
            SCAN_RESULTS,
            IS_ATTACHED,
            STATUS,
            SIGNAL_POLL,
            GET_NETWORK,
            SET_NETWORK,
        };

        enum CtrlEvent {
            CTRL_EVENT_SCAN_RESULTS = 0,
            CTRL_EVENT_SUBNET_STATUS_UPDATE,
            CTRL_EVENT_ASSOC_REJECT,
            CTRL_EVENT_SSID_TEMP_DISABLED,
            CTRL_EVENT_CONNECTED,
            CTRL_EVENT_DISCONNECTED,
            CTRL_EVENT_TERMINATING,
        };

        using wpaCmdFn = std::function<bool(std::string&)>;
        using wpaCmdNetworkFn = std::function<bool(std::string&, int)>;
        using wpaCmdStatusFn = std::function<bool(std::string&, char*)>;
        using wpaCmdGetNetworkFn = std::function<bool(std::string&, int, const char*)>;
        using wpaCmdSetNetworkFn = std::function<bool(std::string&, int, const char*, const char*)>;
        using monitorFn = std::function<void(std::string&)>;

        ~TestWpaClient();

        void setCallback(std::function<void(const char*)> fn) override;
        void clearCallback() override;
        void clearCommands();
        void onCommand(CmdType cmd, wpaCmdFn fn);
        void onCommand(CmdType cmd, wpaCmdNetworkFn fn);
        void onCommand(CmdType cmd, wpaCmdStatusFn fn);
        void onCommand(CmdType cmd, wpaCmdGetNetworkFn fn);
        void onCommand(CmdType cmd, wpaCmdSetNetworkFn fn);
        void scheduleMonitorEvent(uint32_t delayMs, monitorFn fn);

        void startThread() override;
        const char* buffer() const override;
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
        bool isAttached() const override;
        std::string status(char* arg) const override;
        std::string signalPoll() const override;
        std::string scanResults() const override;
        std::string setNetwork(int networkId, const char* varname, const char* value) const override;
        std::string getNetwork(int networkId, const char* varname) const override;
        std::string disconnect() const override;
        std::string reconnect() const override;
        std::string reassociate() const override;

        bool setBuffer(const std::string& buffer = "");

    protected:
        void loopTask() override;

        mutable std::mutex lock_;

        std::function<void(const char*)> callback_;
        std::future<void> eloop_;

        mutable std::string buffer_;
        std::mutex monitorLock_;
        std::atomic<bool> loopTaskRun_;
        std::condition_variable monitorWait_;

        std::unordered_map<uint64_t, std::queue<monitorFn>> monitorFifo_;
        std::unordered_map<CmdType, wpaCmdFn, EnumClassHash> wpaCmd_;
        std::unordered_map<CmdType, wpaCmdNetworkFn, EnumClassHash> wpaCmdNetwork_;
        wpaCmdStatusFn wpaCmdStatus_ = {};
        wpaCmdSetNetworkFn wpaCmdSetNetwork_ = {};
        wpaCmdGetNetworkFn wpaCmdGetNetwork_ = {};
    };

} // namespace quasar
