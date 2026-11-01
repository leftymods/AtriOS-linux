#pragma once

#include "wifi_types.h"

#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/wpa_cli/wpa_client.h>
#include <yandex_io/protos/quasar_proto.pb.h>

#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace quasar {

    class WifiManager {
    public:
        enum {
            EXTRA_NETWORK_INFO = 0,
            EXTRA_SUPPLICANT_ERROR,
        };

        enum class Action {
            SUPPLICANT_STATE_CHANGED_ACTION = 0,
            NETWORK_STATE_CHANGED_ACTION,
            CONNECTIVITY_ACTION,
        };

        static const std::string& Action_Name(const Action& value);

        using CallbackResults = std::function<void(std::vector<ScanResult>)>;
        using CallbackAction = std::function<void(const Action&, const std::unordered_map<std::string, std::string>&)>;

        using WpaStatus = std::unordered_map<std::string, std::string>;

        WifiManager(std::shared_ptr<YandexIO::IDevice> device, WpaClient& wpaClient);
        ~WifiManager();
        WifiManager& operator=(const WifiManager&) = delete;

        void sleepUntilWpaCliAttached();
        bool startScan();
        bool setWifiEnabled(bool flag);
        int addNetwork(const WifiConfiguration& config);
        bool removeNetwork(int netId);
        bool enableNetwork(int netId, bool flag);
        bool enableAllNetworks();
        bool disableAllNetworks();
        bool reloadConfiguration();
        bool saveConfiguration();
        bool reconnect();
        bool disconnect();
        bool reassociate();
        bool isWifiEnabled();
        std::vector<ScanResult> getScanResults();
        std::map<int, WifiConfiguration> getConfiguredNetworks();
        quasar::proto::WifiStatus::Signal calculateSignalLevel(int rssi, int numLevels);
        std::shared_ptr<const WifiConfiguration> getConnectionInfo() const;

        void registerResultsCallback(CallbackResults callback) {
            callbackResults_ = std::move(callback);
        }
        void registerActionCallback(CallbackAction callback) {
            callbackAction_ = std::move(callback);
        }
        void deregisterCallbacks();

        WpaStatus getWpaStatus() const;

        DetailedState getWifiState() const;

        std::string getWpaState() const;

        bool interfaceDisabled() const;

        void enableInterface() const;

    private:
        bool setWifiSignal(WifiConfiguration* network) const;
        WifiConfiguration readNetworkVariables(int networkId);
        std::string getNetworkVariable(int netId, const char* varname);
        template <typename M>
        auto getNetworkVariable(int netId, const char* varname, const M& map) -> std::vector<typename M::mapped_type>;
        void setNetworkVariable(int netId, const char* varname, const std::string& value);
        void setNetworkVariable(int netId, const char* varname, int value);
        template <typename T, typename M>
        void setNetworkVariable(int netId, const char* varname, const T& vec, const M& map);

        static std::string unescapeSSID(const std::string& escapedSSID);

        static std::unordered_map<std::string, std::string> parseReason(const char* buf);

        mutable std::mutex lock_;
        std::shared_ptr<const WifiConfiguration> currentNetwork_;

        bool scanResultsReceived_ = false;

        CallbackResults callbackResults_;
        CallbackAction callbackAction_;

    protected:
        void monitorEvent(const char* buf);

        std::shared_ptr<YandexIO::IDevice> device_;
        WpaClient& wpa_;
    };

} // namespace quasar
