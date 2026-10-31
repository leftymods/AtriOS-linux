#pragma once

#include "wifi_manager.h"
#include "wifi_types.h"

#include <yandex_io/interfaces/device_state/i_device_state_provider.h>
#include <yandex_io/interfaces/user_config/i_user_config_provider.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/delay_timings_policy/delay_timings_policy.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>
#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/threading/unique_callback.h>
#include <yandex_io/libs/http_client/http_client.h>

#include <atomic>
#include <chrono>
#include <optional>

namespace quasar {

    class WifiEndpoint {
        using WifiConfigurationPtr = std::shared_ptr<const WifiConfiguration>;

    public:
        WifiEndpoint(
            std::shared_ptr<YandexIO::IDevice> device,
            std::shared_ptr<ipc::IIpcFactory> ipcFactory,
            std::shared_ptr<IDeviceStateProvider> deviceStateProvider,
            std::shared_ptr<IUserConfigProvider> userConfigProvider,
            quasar::WifiManager& wifiManager);
        ~WifiEndpoint();
        WifiEndpoint& operator=(const WifiEndpoint&) = delete;

        static const std::string SERVICE_NAME;

    private:
        void processQuasarMessage(const ipc::SharedMessage& message,
                                  ipc::IServer::IClientConnection& connection);
        void processWifiListRequest(const ipc::SharedMessage& message,
                                    ipc::IServer::IClientConnection& connection);
        void processWifiDisable(const ipc::SharedMessage& message,
                                ipc::IServer::IClientConnection& connection);
        void processWifiEnable(const ipc::SharedMessage& message,
                               ipc::IServer::IClientConnection& connection);
        void processWifiConnect(const ipc::SharedMessage& message,
                                ipc::IServer::IClientConnection& connection);
        void processAsyncWifiConnect(const ipc::SharedMessage& message);

        static bool isCorporateNetwork(const ScanResult& scanResult);
        static bool isHiddenNetwork(quasar::proto::WifiConnect& wifiConnect);
        quasar::proto::WifiType findWifiType(quasar::proto::WifiConnect& wifiConnect);
        bool waitForConnectivityState(const std::set<quasar::proto::WifiStatus_Status>& states,
                                      bool stopOnAuthError, int timeoutSeconds) const;
        bool waitForInternetReachable(int timeoutSeconds) const;

        void shrinkConfiguredNetworks() const;

        void startScan();
        void scheduleRescan();
        void scheduleScan(std::chrono::seconds timeout);
        /**
         * @brief Callback passed to wpa_supplicant_client. wpa_supplicant_client use this function to provide scan results
         * @param scanResults vector of scan results
         */
        void receiveScanResults(std::vector<ScanResult> scanResults);
        void receiveNetworkStateChange(const WifiManager::Action& action, const std::unordered_map<std::string, std::string>& info);
        void rescanThread();
        void wlan0MonitorThread();

        // generate_204 check methods
        void initInternetChecker();
        void generate204(std::function<void(std::optional<int>)> callback);
        void handleGenerate204Result(int attempt, std::optional<int> responseCode);
        void scheduleGenerate204(std::chrono::milliseconds timeout, int attempt);

        quasar::proto::WifiList buildWifiList() const;
        quasar::proto::WifiStatus buildWifiStatusUnlocked(const WifiConfigurationPtr& wifiConfiguration) const;

        static quasar::proto::WifiStatus_Status getWifiStatus(bool internetReachable, DetailedState connectivityState);
        void updateWifiStatus(bool updateConnectivityState);
        void updateWifiStatus(DetailedState newConnectivityState);
        void updateWifiStatusImplUnlocked(bool notChosenFallbackEnabled, bool updateConnectivityState);
        void broadcastWifiStatusUnlocked(const WifiConfigurationPtr& wifiConfiguration);
        std::string getWifiMetaInfo() const;
        static int calculateSignalLevel(int rssi, int numLevels);
        static int getChannel(int freq);

        void sendHWaddrInfo();

    private:
        Lifetime lifetime_;
        const std::shared_ptr<YandexIO::IDevice> device_;
        std::shared_ptr<NamedCallbackQueue> worker_;
        UniqueCallback scanCallback_;

        bool isScanning_{false};
        std::optional<std::chrono::steady_clock::time_point> lastScanReceivedTs_;
        bool isConfiguringMode_{false};

        std::atomic<bool> internetReachable_{false};
        std::atomic<bool> curlInternetChecker_{true};
        BackoffRetriesWithRandomPolicy backoffHelper_;
        std::shared_ptr<NamedCallbackQueue> generate204Worker_;
        UniqueCallback generate204Callback_;

        bool supplicantAuthError_ = false;
        DetailedState connectivityState_ = DetailedState::DISCONNECTED;
        std::atomic<quasar::proto::WifiStatus_Status> wifiStatus_ = quasar::proto::WifiStatus::NOT_CONNECTED;
        quasar::proto::WifiStatus_Signal signalStatus_ = quasar::proto::WifiStatus::WEAK;
        std::vector<ScanResult> scanResults_;

        std::mutex wlan0MonitorSync_;
        std::mutex wifiStatusSync_;

        SteadyConditionVariable wlan0MonitorCond_;

        std::thread wlan0Monitor_;
        std::atomic_bool stopped_ = {false};
        std::chrono::seconds connectivityStateChangeTimeout_;
        uint32_t maxStoredWifiNetworks_ = 10;

        std::chrono::seconds waitForInternetAfterConnect_;

        std::shared_ptr<ipc::IServer> server_;

        static constexpr std::chrono::seconds GENERATE204_DEFAULT_PERIOD = std::chrono::seconds(10);
        static constexpr std::chrono::seconds GENERATE204_SPEDUP_PERIOD = std::chrono::seconds(2);

        static constexpr int NUM_RSSI_LEVELS = 4;
        /* Default Timeout between wifi rescans */
        static constexpr std::chrono::seconds DEFAULT_RESCAN_TIMEOUT = std::chrono::hours(1);
        static constexpr std::chrono::seconds DEFAULT_CONFIGURING_RESCAN_TIMEOUT = std::chrono::seconds(10);

        std::chrono::seconds defaultRescanTimeout_{DEFAULT_RESCAN_TIMEOUT};
        std::chrono::seconds rescanTimeout_{DEFAULT_RESCAN_TIMEOUT};
        std::chrono::seconds configuringRescanTimeout_{DEFAULT_CONFIGURING_RESCAN_TIMEOUT};
        std::chrono::seconds minRescanTimeout_{DEFAULT_CONFIGURING_RESCAN_TIMEOUT};

        // call reassociate after enableNetwork or not
        bool reassociateAfterEnable_{true};

        quasar::WifiManager& wifiManager_;

        std::shared_ptr<IDeviceStateProvider> deviceStateProvider_;
        std::shared_ptr<IUserConfigProvider> userConfigProvider_;
    };

} /* namespace quasar */
