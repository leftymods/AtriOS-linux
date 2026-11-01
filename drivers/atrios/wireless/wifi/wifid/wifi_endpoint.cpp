#include "wifi_endpoint.h"

#include "wifi_manager.h"
#include "wifi_utils.h"
#include "internet_checker.h"

#include <yandex_io/libs/base/crc32.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/protobuf_utils/debug.h>
#include <yandex_io/protos/quasar_proto.pb.h>
#include <yandex_io/protos/enum_names/enum_names.h>

#include <boost/algorithm/string.hpp>

#include <util/system/yassert.h>

#include <algorithm>
#include <cstdlib>
#include <memory>

YIO_DEFINE_LOG_MODULE("wifi");

using namespace quasar;

/* static variables init */
const std::string WifiEndpoint::SERVICE_NAME = "wifid";

namespace {
    bool isConnectedStatus(proto::WifiStatus_Status status) {
        return status == proto::WifiStatus::CONNECTED || status == proto::WifiStatus::CONNECTED_NO_INTERNET;
    }
} // namespace

WifiEndpoint::WifiEndpoint(
    std::shared_ptr<YandexIO::IDevice> device,
    std::shared_ptr<ipc::IIpcFactory> ipcFactory,
    std::shared_ptr<IDeviceStateProvider> deviceStateProvider,
    std::shared_ptr<IUserConfigProvider> userConfigProvider,
    WifiManager& wifiManager)
    : device_(std::move(device))
    , worker_(std::make_shared<NamedCallbackQueue>("WifiWorker"))
    , scanCallback_(worker_, quasar::UniqueCallback::ReplaceType::INSERT_BACK)
    , generate204Worker_(std::make_shared<NamedCallbackQueue>("WifiInternetWorker"))
    , generate204Callback_(generate204Worker_, quasar::UniqueCallback::ReplaceType::INSERT_BACK)
    , server_(ipcFactory->createIpcServer(SERVICE_NAME))
    , wifiManager_(wifiManager)
    , deviceStateProvider_(std::move(deviceStateProvider))
    , userConfigProvider_(std::move(userConfigProvider))
{
    Y_VERIFY(deviceStateProvider_);
    Y_VERIFY(userConfigProvider_);

    YIO_LOG_INFO("WifiManager service started");

    sendHWaddrInfo();

    server_->setMessageHandler(std::bind(&WifiEndpoint::processQuasarMessage, this, std::placeholders::_1, std::placeholders::_2));
    server_->setClientConnectedHandler([=](auto& connection) {
        proto::QuasarMessage startupMessage;
        startupMessage.mutable_wifi_status()->CopyFrom(buildWifiStatusUnlocked(wifiManager_.getConnectionInfo()));
        connection.send(std::move(startupMessage));

        worker_->add([this, connection{connection.share()}] {
            connection->send(quasar::ipc::buildMessage([this](auto& msg) {
                *msg.mutable_wifi_list() = buildWifiList();
            }));
        });
    });

    const auto& config = device_->configuration()->getServiceConfig(SERVICE_NAME);

    // Some devices may fail to reassociate properly. Flag to disable reassociate
    reassociateAfterEnable_ = tryGetBool(config, "reassociateAfterEnable", reassociateAfterEnable_);

    /* save default value */
    defaultRescanTimeout_ = tryGetSeconds(config, "rescanTimeoutSec", DEFAULT_RESCAN_TIMEOUT);
    /*set up current timeout */
    rescanTimeout_ = defaultRescanTimeout_;
    configuringRescanTimeout_ = tryGetSeconds(config, "configuringRescanTimeoutSec", DEFAULT_CONFIGURING_RESCAN_TIMEOUT);
    // use configuringRescanTimeout_ as minimum timeout between scans, since it's a "short" timeout
    Y_VERIFY(defaultRescanTimeout_ >= configuringRescanTimeout_);
    minRescanTimeout_ = configuringRescanTimeout_;

    waitForInternetAfterConnect_ = std::chrono::seconds(tryGetInt(config, "waitForInternetAfterConnectSec", 15));
    connectivityStateChangeTimeout_ = std::chrono::seconds(tryGetInt(config, "connectivityStateChangeTimeoutSeconds", 40));
    maxStoredWifiNetworks_ = tryGetUInt32(config, "maxStoredWifiNetworks", 10);

    YIO_LOG_DEBUG("wifiManager.sleepUntilWpaCliAttached()");
    wifiManager_.sleepUntilWpaCliAttached();
    YIO_LOG_DEBUG("wifiManager.sleepUntilWpaCliAttached() Done");

    wifiManager_.registerResultsCallback([this](auto scanResults) mutable {
        worker_->add([this, scanResults{std::move(scanResults)}]() mutable {
            receiveScanResults(std::move(scanResults));
        });
    });
    wifiManager_.registerActionCallback([this](const WifiManager::Action& action, const std::unordered_map<std::string, std::string>& info) {
        worker_->add([this, action, info]() {
            receiveNetworkStateChange(action, info);
        });
    });

    // start scan loop
    scheduleScan(std::chrono::seconds(0));

    updateWifiStatus(wifiManager_.getWifiState());

    server_->listenService(); // This should not be moved higher, since this starts accepting connects and might act on partially initialized state

    initInternetChecker();

    wlan0Monitor_ = std::thread(&WifiEndpoint::wlan0MonitorThread, this);

    deviceStateProvider_->configurationChangedSignal().connect(
        [this](const auto& deviceState) {
            const auto isConfiguring = deviceState->configuration == DeviceState::Configuration::CONFIGURING;
            if (isConfiguring == isConfiguringMode_) {
                return;
            }
            isConfiguringMode_ = isConfiguring;
            if (isConfiguring) {
                YIO_LOG_INFO("Turn on WiFi rescanning...");
                scheduleScan(std::chrono::seconds(0));
            } else {
                YIO_LOG_INFO("Turn off WiFi rescanning...");
                // reschedule next scan with "long" timeout
                scheduleRescan();
            }
        }, lifetime_, worker_);

    userConfigProvider_->jsonChangedSignal(IUserConfigProvider::ConfigScope::SYSTEM, "wifiSettings").connect([this](const auto& wifiSettingsPtr) {
        const auto& wifiSettings = *wifiSettingsPtr;
        {
            // Update Rescan timeout
            const auto defaultTimeoutMs = std::chrono::milliseconds(defaultRescanTimeout_);
            // get ms for backward compatibility
            const auto newRescanTimeoutMs = tryGetMillis(wifiSettings, "rescanTimeoutMs", defaultTimeoutMs);
            const auto newRescanTimeout = std::chrono::duration_cast<std::chrono::seconds>(newRescanTimeoutMs);
            if (newRescanTimeout != rescanTimeout_) {
                YIO_LOG_INFO("Wifi Rescan timeout changed. New timeout: " << newRescanTimeout.count() << " sec");
                rescanTimeout_ = newRescanTimeout;
            }
        }

        curlInternetChecker_ = !tryGetBool(wifiSettings, "generic204", false);

        {
            // Update generate204 settings
            const auto generate204 = tryGetJson(wifiSettings, "generate204");
            const auto defaultPeriod = tryGetSeconds(generate204, "periodSec", GENERATE204_DEFAULT_PERIOD);
            YIO_LOG_INFO("Apply new generate204 periods:" << jsonToString(generate204));
            backoffHelper_.updateCheckPeriod(defaultPeriod, GENERATE204_SPEDUP_PERIOD);
        }
    }, lifetime_, worker_);
}

WifiEndpoint::~WifiEndpoint() {
    lifetime_.die();
    wifiManager_.deregisterCallbacks();
    stopped_ = true;
    wlan0MonitorCond_.notify_all();
    if (wlan0Monitor_.joinable()) {
        wlan0Monitor_.join();
    }
    worker_->destroy();
    generate204Worker_->destroy();
}

void WifiEndpoint::initInternetChecker() {
    const uint32_t seed = getCrc32(device_->deviceId()) + getNowTimestampMs();
    backoffHelper_ = BackoffRetriesWithRandomPolicy(seed);
    constexpr auto maxUpdatePeriod = std::chrono::hours(1); // big enough value for experiments
    backoffHelper_.initCheckPeriod(GENERATE204_DEFAULT_PERIOD, GENERATE204_SPEDUP_PERIOD, maxUpdatePeriod);

    // use separate thread for http requests (timeout is up 2 sec)
    scheduleGenerate204(std::chrono::seconds(0), 0);
}

void WifiEndpoint::processQuasarMessage(const ipc::SharedMessage& message, ipc::IServer::IClientConnection& connection) {
    YIO_LOG_INFO("Start Process QuasarMessage: " << shortUtf8DebugString(*message));

    const auto startTs = std::chrono::steady_clock::now();

    if (message->has_wifi_list_request()) {
        processWifiListRequest(message, connection);
    }
    if (message->has_wifi_connect()) {
        processWifiConnect(message, connection);
    }
    if (message->has_async_wifi_connect()) {
        processAsyncWifiConnect(message);
    }
    if (message->has_wifi_config_reload()) {
        wifiManager_.reloadConfiguration();
    }
    if (message->has_wifi_networks_enable()) {
        processWifiEnable(message, connection);
    } else if (message->has_wifi_networks_disable()) {
        processWifiDisable(message, connection);
    }

    if (message->has_reset_wifi()) {
        wifiManager_.setWifiEnabled(false);
        wifiManager_.setWifiEnabled(true);
    }
    const auto stopTs = std::chrono::steady_clock::now();
    const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(stopTs - startTs);

    YIO_LOG_INFO("Process QuasarMessage End. Process time: " << diff.count() << "ms");
}

void WifiEndpoint::processWifiListRequest(const ipc::SharedMessage& message, ipc::IServer::IClientConnection& connection) {
    worker_->add([this, reqId{message->request_id()}, connection = connection.share()]() mutable {
        YIO_LOG_INFO("scanResults_.size(): " << scanResults_.size());
        connection->send(quasar::ipc::buildMessage([&](auto& msg) {
            msg.set_request_id(std::move(reqId));
            *msg.mutable_wifi_list() = buildWifiList();
        }));
    });
}

proto::WifiList WifiEndpoint::buildWifiList() const {
    Y_ENSURE_THREAD(worker_);

    proto::WifiList wifiList;

    std::string currentNetworkBSSID;
    if (const auto wifiConfiguration = wifiManager_.getConnectionInfo()) {
        currentNetworkBSSID = wifiConfiguration->BSSID;
    }

    if (!scanResults_.empty()) {
        std::unordered_set<std::string> ssids;
        for (const auto& scanResult : scanResults_) {
            YIO_LOG_DEBUG("   " << scanResult.to_string());
            if (ssids.find(scanResult.SSID) != ssids.end()) {
                continue;
            }

            auto wifiInfo = wifiList.add_hotspots();
            wifiInfo->set_wifi_type(WifiUtils::parseType(scanResult.capabilities));
            wifiInfo->set_is_corporate(isCorporateNetwork(scanResult));
            const bool isSecure = wifiInfo->wifi_type() == proto::WifiType::WEP ||
                                  wifiInfo->wifi_type() == proto::WifiType::WPA;
            wifiInfo->set_ssid(TString(scanResult.SSID));
            wifiInfo->set_secure(isSecure);
            wifiInfo->set_rssi(scanResult.rssi);
            wifiInfo->set_mac(TString(scanResult.BSSID));
            wifiInfo->set_is_connected(scanResult.BSSID == currentNetworkBSSID);
            wifiInfo->set_freq(scanResult.rate);
            wifiInfo->set_channel(getChannel(scanResult.rate));
            ssids.insert(scanResult.SSID);
        }
    }
    return wifiList;
}

proto::WifiStatus WifiEndpoint::buildWifiStatusUnlocked(const WifiConfigurationPtr& wifiConfiguration) const {
    proto::WifiStatus wifiStatus;
    wifiStatus.set_status(wifiStatus_);
    wifiStatus.set_internet_reachable(internetReachable_);
    wifiStatus.set_signal(signalStatus_);

    if (wifiConfiguration != nullptr && !wifiConfiguration->empty()) {
        auto currentNetwork = wifiStatus.mutable_current_network();
        currentNetwork->set_ssid(TString(wifiConfiguration->SSID));
        currentNetwork->set_mac(TString(wifiConfiguration->BSSID));
        currentNetwork->set_freq(wifiConfiguration->frequency);
        currentNetwork->set_channel(getChannel(wifiConfiguration->frequency));
    }

    return wifiStatus;
}

/*
   This is according to http://w1.fi/cgit/hostap/tree/wpa_supplicant/ctrl_iface.c?id=7b42862ac87f333b0efb0f0bae822dcdf606bc69#n2034
*/
bool WifiEndpoint::isCorporateNetwork(const ScanResult& scanResult) {
    return scanResult.capabilities.find("EAP") != std::string::npos;
}

void WifiEndpoint::processWifiDisable(const ipc::SharedMessage& message, ipc::IServer::IClientConnection& connection) {
    YIO_LOG_DEBUG("Disabling all wifi networks");
    wifiManager_.disableAllNetworks();
    YIO_LOG_DEBUG("All networks are disabled, waiting for DISCONNECTED state");

    waitForConnectivityState({proto::WifiStatus::NOT_CHOSEN, proto::WifiStatus::NOT_CONNECTED}, false, connectivityStateChangeTimeout_.count());
    YIO_LOG_DEBUG("Disconnected. Sending response");

    proto::QuasarMessage response;
    response.set_request_id(message->request_id());

    connection.send(std::move(response));
}

void WifiEndpoint::processWifiEnable(const ipc::SharedMessage& message, ipc::IServer::IClientConnection& connection) {
    YIO_LOG_DEBUG("Enabling all wifi networks");
    wifiManager_.enableAllNetworks();
    wifiManager_.saveConfiguration();

    proto::QuasarMessage response;
    response.set_request_id(message->request_id());

    connection.send(std::move(response));
}

void WifiEndpoint::processWifiConnect(const ipc::SharedMessage& message, ipc::IServer::IClientConnection& connection) {
    proto::WifiConnect wifiConnect = message->wifi_connect();
    std::string ssid = wifiConnect.wifi_id();
    std::string password = wifiConnect.password();

    YIO_LOG_INFO("Connecting to wifi " << ssid);

    proto::QuasarMessage response;
    response.set_request_id(message->request_id());

    proto::WifiConnectResponse_Status status = proto::WifiConnectResponse::TIMEOUT;

    do {
        shrinkConfiguredNetworks();

        supplicantAuthError_ = false;

        proto::WifiType type = findWifiType(wifiConnect);

        if (type == proto::WifiType::UNKNOWN_WIFI_TYPE) {
            status = proto::WifiConnectResponse::SSID_NOT_FOUND;
            break;
        }

        bool hidden = isHiddenNetwork(wifiConnect);
        int networkId = WifiUtils::startWifiConnect(ssid, password, type, hidden, wifiManager_, reassociateAfterEnable_);
        if (networkId == -1) {
            YIO_LOG_DEBUG("addNetwork returned -1");
            break;
        }

        YIO_LOG_INFO("Waiting for CONNECTED state");
        bool success = waitForConnectivityState({proto::WifiStatus::CONNECTED, proto::WifiStatus::CONNECTED_NO_INTERNET}, true, connectivityStateChangeTimeout_.count());

        if (success) {
            YIO_LOG_INFO("Connected to wifi");
            status = proto::WifiConnectResponse::OK;
            wifiManager_.saveConfiguration();
        } else {
            if (supplicantAuthError_) {
                status = proto::WifiConnectResponse::AUTH_ERROR;
                YIO_LOG_ERROR_EVENT("WifiEndpoint.ConnectAuthError", "Supplicant auth error");
            } else {
                status = proto::WifiConnectResponse::TIMEOUT;
                YIO_LOG_ERROR_EVENT("WifiEndpoint.ConnectTimeout", "Wifi connect timeout");
            }
            wifiManager_.removeNetwork(networkId);
        }

    } while (false);

    scheduleGenerate204(std::chrono::seconds(0), 0);
    waitForInternetReachable(waitForInternetAfterConnect_.count());

    const Json::Value& common = device_->configuration()->getServiceConfig("common");
    std::string server = "ru.pool.ntp.org";
    if (common.isMember("ntpServerList")) {
        const Json::Value& serverList = common["ntpServerList"];
        server = serverList[rand() % serverList.size()].asString();
    }

    if (common.isMember("ntpClientCommand")) {
        std::string command = getString(common, "ntpClientCommand") + " " + server;
        int ntpdateStatus = system(command.c_str());
        YIO_LOG_INFO("\"" + command + "\" returned " << ntpdateStatus);
    } else {
        YIO_LOG_WARN("No ntpClientCommand provided in config");
    }

    YIO_LOG_DEBUG("Sending connect response");

    auto wifi = response.mutable_wifi_connect_response();
    wifi->set_status(status);

    connection.send(std::move(response));
}

void WifiEndpoint::shrinkConfiguredNetworks() const {
    auto networks = wifiManager_.getConfiguredNetworks();

    while (networks.size() >= maxStoredWifiNetworks_) {
        auto network = networks.begin();
        YIO_LOG_DEBUG("Removing old network, id: " << network->second.networkId << ", ssid: " << network->second.SSID);
        wifiManager_.removeNetwork(network->second.networkId);
        networks.erase(network);
    }
}

void WifiEndpoint::processAsyncWifiConnect(const ipc::SharedMessage& message) {
    proto::WifiConnect wifiConnect = message->async_wifi_connect();
    std::string ssid = "\"" + wifiConnect.wifi_id() + "\"";
    std::string password = wifiConnect.password();

    YIO_LOG_INFO("Async connect to ssid " << ssid);

    device_->telemetry()->reportEvent("asyncWifiConnect");

    auto networks = wifiManager_.getConfiguredNetworks();
    auto network = std::find_if(networks.cbegin(), networks.cend(), [&](auto e) { return e.second.SSID == ssid; });
    if (network != networks.cend()) {
        YIO_LOG_INFO("Network found in saved networks. Enabling it");
        device_->telemetry()->reportEvent("asyncWifiConnectFoundInSaved");
        wifiManager_.enableNetwork(network->second.networkId, true);
        if (reassociateAfterEnable_) {
            wifiManager_.reassociate();
        }
        return;
    }

    YIO_LOG_INFO("Network not found in saved networks. Creating it");
    device_->telemetry()->reportEvent("asyncWifiConnectNotFoundInSaved");
    shrinkConfiguredNetworks();
    proto::WifiType type = findWifiType(wifiConnect);
    if (type == proto::WifiType::UNKNOWN_WIFI_TYPE) {
        YIO_LOG_INFO("Cannot find ssid " << ssid << " during async connect");
        return;
    }
    bool hidden = isHiddenNetwork(wifiConnect);
    int networkId = WifiUtils::startWifiConnect(ssid, password, type, hidden, wifiManager_, reassociateAfterEnable_);
    if (networkId == -1) {
        YIO_LOG_INFO("addNetwork returned -1");
    }
}

bool WifiEndpoint::isHiddenNetwork(proto::WifiConnect& wifiConnect) {
    return wifiConnect.has_wifi_type() && wifiConnect.wifi_type() != proto::WifiType::UNKNOWN_WIFI_TYPE;
}

proto::WifiType WifiEndpoint::findWifiType(proto::WifiConnect& wifiConnect) {
    YIO_LOG_DEBUG("scanResults_.size(): " << scanResults_.size());

    if (wifiConnect.has_wifi_type() && wifiConnect.wifi_type() != proto::WifiType::UNKNOWN_WIFI_TYPE) {
        YIO_LOG_DEBUG(
            "wifiConnect.has_wifi_type() && wifiConnect.wifi_type() != proto::WifiType::UNKNOWN_WIFI_TYPE === true");
        return wifiConnect.wifi_type();
    } else {
        ScanResult foundScanResult;

        {
            YIO_LOG_INFO("Trying to identify wifi type from scan results");
            auto scanResults = wifiManager_.getScanResults();
            YIO_LOG_DEBUG("local scanResults.size(): " << scanResults.size());

            for (auto& scanResult : scanResults) {
                if (scanResult.SSID == wifiConnect.wifi_id()) {
                    foundScanResult = scanResult;
                    break;
                }
            }

            // scanResults_ should be used inside worker_ thread
            worker_->add([this, scanResults{std::move(scanResults)}]() mutable {
                scanResults_ = std::move(scanResults);
            });
        }

        if (foundScanResult.empty()) {
            YIO_LOG_INFO("Network " << wifiConnect.wifi_id() << " not found in scan results");
            return proto::WifiType::UNKNOWN_WIFI_TYPE;
        }

        YIO_LOG_INFO("Connecting to wifi " << foundScanResult.to_string());
        proto::WifiType type = WifiUtils::parseType(foundScanResult.capabilities);
        YIO_LOG_INFO("WifiType: " << wifiTypeName(type));
        return type;
    }
}

bool WifiEndpoint::waitForConnectivityState(const std::set<quasar::proto::WifiStatus_Status>& states, bool stopOnAuthError,
                                            int timeoutSeconds) const {
    for (int i = 0; i < timeoutSeconds * 10 && !stopped_; ++i) {
        if (states.find(wifiStatus_.load()) != states.end()) {
            return true;
        } else if (supplicantAuthError_ && stopOnAuthError) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

bool WifiEndpoint::waitForInternetReachable(int timeoutSeconds) const {
    YIO_LOG_INFO("Waiting for Internet");
    int seconds;
    for (seconds = 0; seconds < timeoutSeconds * 10 && !stopped_; ++seconds) {
        if (internetReachable_) {
            YIO_LOG_INFO("Waiting for Internet done in " << seconds / 10.0 << " seconds");
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    YIO_LOG_WARN("Still no Internet after " << timeoutSeconds << "seconds!");
    return false;
}

void WifiEndpoint::receiveScanResults(std::vector<ScanResult> scanResults) {
    Y_ENSURE_THREAD(worker_);

    YIO_LOG_INFO("Wifi scan finished");
    isScanning_ = false;
    lastScanReceivedTs_ = std::chrono::steady_clock::now();

    std::sort(scanResults.begin(), scanResults.end(), [](const ScanResult& a, const ScanResult& b) -> bool {
        return a.rssi > b.rssi;
    });

    if (YIO_LOG_DEBUG_ENABLED()) {
        YIO_LOG_DEBUG("scanResults_ updated. scanResults_.size(): " << scanResults.size());
        for (auto& scanResult : scanResults) {
            YIO_LOG_DEBUG("  - " << scanResult.to_string());
        }
    }

    scanResults_ = std::move(scanResults);

    /* Broadcast scan results */
    server_->sendToAll(quasar::ipc::buildMessage([this](auto& msg) {
        *msg.mutable_wifi_list() = buildWifiList();
    }));

    // schedule next wifi scan
    scheduleRescan();
}

void WifiEndpoint::startScan() {
    Y_ENSURE_THREAD(worker_);

    if (isScanning_) {
        return;
    }

    if (lastScanReceivedTs_.has_value()) {
        const auto diff = std::chrono::steady_clock::now() - *lastScanReceivedTs_;
        // scanning 2 times in a row cause problems. Backoff next scan using "short" timeout
        if (diff < minRescanTimeout_) {
            const auto timeLeft = minRescanTimeout_ - diff;
            scheduleScan(std::chrono::duration_cast<std::chrono::seconds>(timeLeft));
            return;
        }
    }

    isScanning_ = wifiManager_.startScan();
    if (isScanning_) {
        YIO_LOG_INFO("Started wifi scan");
    } else {
        YIO_LOG_WARN("Start WiFi scan failed");
        // ensure scan loop continues in error case
        constexpr auto retryScanTimeout = std::chrono::seconds(10);
        scheduleScan(retryScanTimeout);
    }
}

void WifiEndpoint::scheduleScan(std::chrono::seconds timeout) {
    scanCallback_.executeDelayed([this]() {
        startScan();
    }, timeout, lifetime_);
}

void WifiEndpoint::scheduleRescan() {
    Y_ENSURE_THREAD(worker_);
    const auto rescanTimeout = isConfiguringMode_ ? configuringRescanTimeout_ : rescanTimeout_;
    scheduleScan(rescanTimeout);
}

void WifiEndpoint::receiveNetworkStateChange(const WifiManager::Action& action, const std::unordered_map<std::string, std::string>& info) {
    Y_ENSURE_THREAD(worker_);

    std::optional<DetailedState> networkState;
    switch (action) {
        case WifiManager::Action::SUPPLICANT_STATE_CHANGED_ACTION: {
            auto ctrl = info.at("ctrl");
            YIO_LOG_INFO(std::string("Supplicant state changed info: ") + info.at("ctrl"));
            if ("CTRL-EVENT-CONNECTED" == ctrl) {
                networkState = DetailedState::CONNECTED;
            } else if ("CTRL-EVENT-DISCONNECTED" == ctrl) {
                networkState = DetailedState::DISCONNECTED;
            }
            break;
        }
        case WifiManager::Action::NETWORK_STATE_CHANGED_ACTION: {
            auto ctrl = info.at("ctrl");
            if ("CTRL-EVENT-ASSOC-REJECT" == ctrl) {
                networkState = DetailedState::DISCONNECTED;
            } else if ("CTRL-EVENT-SUBNET-STATUS-UPDATE" == ctrl) {
                // TODO fix me. This is quick fix due to https://st.yandex-team.ru/QUASAR-1993
                //  networkState = DetailedState::AUTHENTICATING;
                break;
            } else if ("CTRL-EVENT-CONNECTED" == ctrl) {
                networkState = DetailedState::CONNECTED;
            } else if ("CTRL-EVENT-SCAN-STARTED" == ctrl) {
                networkState = DetailedState::SCANNING;
            } else if ("CTRL-EVENT-SSID-TEMP-DISABLED" == ctrl) {
                networkState = DetailedState::DISCONNECTED;
                if ("WRONG_KEY" == info.at("reason")) {
                    supplicantAuthError_ = true;
                }
            } else {
                return;
            }

            break;
        }
        default:
            break;
    }

    if (networkState.has_value()) {
        if (*networkState == DetailedState::DISCONNECTED) {
            internetReachable_ = false; // reset flag
        }
        updateWifiStatus(networkState.value());
        YIO_LOG_INFO(std::string("Network state was changed: ") + DetailedState_Name(networkState.value()));
    }
}

void WifiEndpoint::generate204(std::function<void(std::optional<int>)> callback) {
    Y_ENSURE_THREAD(generate204Worker_);

    auto makeInternetChecker = [this]() {
        const auto& commonConfig = device_->configuration()->getCommonConfig();
        const auto host = getString(commonConfig, "robotBackendHost");
        const auto path = "/generate_204";
        if (curlInternetChecker_) {
            return makeCurlInternetChecker(device_, host, path);
        }
        return makeGenericInternetChecker(device_, host, path);
    };

    YIO_LOG_DEBUG("Check generate204");
    std::optional<int> responseCode;
    try {
        auto internetChecker = makeInternetChecker();
        const auto response = internetChecker->check();
        responseCode = response.responseCode;
        YIO_LOG_DEBUG("internet checker return code " << responseCode);
    } catch (const std::runtime_error& e) {
        YIO_LOG_WARN("Internet check FAIL got error: " << e.what());
    }
    callback(responseCode);
}

void WifiEndpoint::scheduleGenerate204(std::chrono::milliseconds timeout, int attempt) {
    generate204Callback_.executeDelayed([this, attempt]() {
        generate204([this, attempt](auto responseCode) {
            // result should be handled in main thread
            worker_->add([this, attempt, responseCode]() {
                handleGenerate204Result(attempt, responseCode);
            });
        });
    }, timeout, lifetime_);
}

void WifiEndpoint::handleGenerate204Result(int attempt, std::optional<int> responseCode) {
    Y_ENSURE_THREAD(worker_);

    constexpr int attemptsCount = 3;
    // check if retry needed
    if (!responseCode && attempt < attemptsCount - 1) {
        scheduleGenerate204(std::chrono::milliseconds(500), attempt + 1);
        return;
    }

    // internet is reachable when generate_204 returns expected result code.
    // Ignore other codes (i.e. router redirects)
    const bool internetReachable = responseCode.value_or(0) == 204;
    const bool internetWasReachable = internetReachable_.exchange(internetReachable);

    updateWifiStatus(internetReachable != internetWasReachable);

    if (internetReachable != internetWasReachable) {
        const std::string oldStatus = internetWasReachable ? "OK" : "FAIL";
        const std::string newStatus = internetReachable ? "OK" : "FAIL";
        YIO_LOG_INFO("Generate204: internet reachable status changed from \"" << oldStatus << "\" to \"" << newStatus << "\"");
    }

    if (isConfiguringMode_ && !internetReachable) {
        // Use fast retries while configuring mode
        backoffHelper_.spedUpDelayBetweenCalls();
    } else if (responseCode && !isSuccessHttpCode(*responseCode)) {
        YIO_LOG_WARN("Internet check FAIL: got response code: " << *responseCode);
        backoffHelper_.increaseDelayBetweenCalls();
    } else if ((!internetReachable && internetWasReachable) || wifiStatus_.load() == proto::WifiStatus::NOT_CHOSEN) {
        // If we are experiencing connectivity problems - shorten the check's timeout
        backoffHelper_.spedUpDelayBetweenCalls();
    } else {
        backoffHelper_.resetDelayBetweenCallsToDefault();
    }

    // next connectivity check cycle
    scheduleGenerate204(backoffHelper_.getDelayBetweenCalls(), 0);
}

void WifiEndpoint::wlan0MonitorThread() {
    while (!stopped_) {
        const auto wpaStatus = wifiManager_.getWpaStatus();

        auto it = wpaStatus.find("wpa_state");
        if (it != wpaStatus.end()) {
            if (it->second == "INTERFACE_DISABLED") {
                wifiManager_.enableInterface();
            }
        }

        std::unique_lock<std::mutex> lk(wlan0MonitorSync_);
        wlan0MonitorCond_.wait_for(lk, std::chrono::seconds(10), [=] { return stopped_.load(); });
    }
}

proto::WifiStatus_Status WifiEndpoint::getWifiStatus(
    bool internetReachable, DetailedState connectivityState) {
    // internetReachable updates each 10 sec. DetailedState can be disconnected while
    // internetReachable is still true. So force NOT_CONNECTED state
    if (connectivityState == DetailedState::DISCONNECTED) {
        return proto::WifiStatus::NOT_CONNECTED;
    }
    if (internetReachable) {
        return proto::WifiStatus::CONNECTED;
    }

    switch (connectivityState) {
        case DetailedState::CONNECTED:
            return proto::WifiStatus::CONNECTED_NO_INTERNET;
        case DetailedState::CONNECTING:
        case DetailedState::AUTHENTICATING:
        case DetailedState::CAPTIVE_PORTAL_CHECK:
        case DetailedState::OBTAINING_IPADDR:
        case DetailedState::SCANNING:
            return proto::WifiStatus::CONNECTING;
        default:
            return proto::WifiStatus::NOT_CONNECTED;
    }
}

void WifiEndpoint::updateWifiStatus(DetailedState newConnectivityState) {
    std::lock_guard<std::mutex> lock(wifiStatusSync_);

    if (connectivityState_ != newConnectivityState) {
        connectivityState_ = newConnectivityState;
        updateWifiStatusImplUnlocked(false, false);
    }
}

void WifiEndpoint::updateWifiStatus(bool updateConnectivityState) {
    std::lock_guard<std::mutex> lock(wifiStatusSync_);

    updateWifiStatusImplUnlocked(true, updateConnectivityState);
}

void WifiEndpoint::updateWifiStatusImplUnlocked(bool notChosenFallbackEnabled, bool updateConnectivityState) {
    const auto wifiConfiguration = wifiManager_.getConnectionInfo();

    if (wifiConfiguration == nullptr && notChosenFallbackEnabled) {
        // Emulating previous behavior that connectivityState changes cant fallback to not_chosen
        wifiStatus_ = proto::WifiStatus::NOT_CHOSEN;
        broadcastWifiStatusUnlocked(wifiConfiguration);
        return;
    }

    if (updateConnectivityState) {
        connectivityState_ = wifiManager_.getWifiState();
    }
    const auto newWifiStatus = getWifiStatus(internetReachable_, connectivityState_);
    const auto newSignalStatus = wifiConfiguration == nullptr ? signalStatus_ : wifiManager_.calculateSignalLevel(wifiConfiguration->rssi, proto::WifiStatus::Signal_ARRAYSIZE);

    YIO_LOG_DEBUG("signalStatus_=" << signalStatus_ << ", wifiStatus_=" << wifiStatusStatusName(wifiStatus_)
                                   << ", newSignalStatus=" << newSignalStatus << ", newWifiStatus=" << wifiStatusStatusName(newWifiStatus)
                                   << ", internetReachable_=" << internetReachable_
                                   << ", connectivityState_=" << DetailedState_Name(connectivityState_));

    if (newWifiStatus != wifiStatus_ || newSignalStatus != signalStatus_) {
        bool wifiDisconnected = false;
        bool wifiConnected = false;
        if (isConnectedStatus(wifiStatus_) && !isConnectedStatus(newWifiStatus)) {
            YIO_LOG_DEBUG("Wifi status changed to disconnected");
            wifiDisconnected = true;
        }
        if (isConnectedStatus(newWifiStatus) && !isConnectedStatus(wifiStatus_)) {
            YIO_LOG_INFO("Wifi status changed to connected");
            wifiConnected = true;
        }

        wifiStatus_ = newWifiStatus;
        signalStatus_ = newSignalStatus;
        YIO_LOG_DEBUG("Wifi changed: wifiStatus_=" << wifiStatusStatusName(wifiStatus_) << ", signalStatus_=" << signalStatus_);

        const std::string wifiMetaInfo = getWifiMetaInfo();
        if (wifiDisconnected) {
            device_->telemetry()->deleteAppEnvironmentValue("wifi_ssid");
            device_->telemetry()->deleteAppEnvironmentValue("wifi_freq");
            device_->telemetry()->reportEvent("wifiDisconnect", wifiMetaInfo);
        } else {
            // It's legal to reach this code when there is no current network
            // so just do nothing in this case
            // TODO : Change thread model to ICallbackQueue

            if (wifiConfiguration != nullptr) {
                device_->telemetry()->putAppEnvironmentValue("wifi_ssid", wifiConfiguration->SSID);
                if (wifiConfiguration->frequency > 0) {
                    device_->telemetry()->putAppEnvironmentValue("wifi_freq", std::to_string(wifiConfiguration->frequency));
                }
            }
        }

        device_->telemetry()->reportEvent("wifiStatus", wifiMetaInfo);
        if (wifiConnected) {
            device_->telemetry()->reportEvent("wifiConnect", wifiMetaInfo);
        }

        broadcastWifiStatusUnlocked(wifiConfiguration);
    }
}

void WifiEndpoint::broadcastWifiStatusUnlocked(const WifiConfigurationPtr& wifiConfiguration) {
    proto::QuasarMessage broadcast;
    broadcast.mutable_wifi_status()->CopyFrom(buildWifiStatusUnlocked(wifiConfiguration));
    server_->sendToAll(std::move(broadcast));
}

std::string WifiEndpoint::getWifiMetaInfo() const {
    Json::Value info;
    info["signal"] = signalStatus_;
    info["speed"] = quasar::proto::WifiStatus::UNKNOWN;
    info["wifi"] = wifiStatus_.load();

    if (const auto wifiConfiguration = wifiManager_.getConnectionInfo()) {
        if (!wifiConfiguration->empty()) {
            info["ssid"] = wifiConfiguration->SSID;
            if (wifiConfiguration->rssi < 0) {
                info["level"] = calculateSignalLevel(wifiConfiguration->rssi, NUM_RSSI_LEVELS);
            }
            if (wifiConfiguration->frequency > 0) {
                info["freq"] = wifiConfiguration->frequency;
                info["channel"] = getChannel(wifiConfiguration->frequency);
            }
            if (wifiConfiguration->linkspeed > 0) {
                info["linkspeed"] = wifiConfiguration->linkspeed;
            }
            info["rssi"] = wifiConfiguration->rssi;
        }
    }

    return Json::FastWriter().write(info);
}

void WifiEndpoint::sendHWaddrInfo() {
    std::string ifconfig;
    bool executedCorrectly = true;
    try {
        ifconfig = executeWithOutput("ifconfig -a");
    } catch (const std::runtime_error& e) {
        YIO_LOG_ERROR_EVENT("WifiEndpoint.FailedRunIfconfig", "Can't run ifconfig, error: " << e.what());
        device_->telemetry()->reportError("hwAddrInfoFailed");
        executedCorrectly = false;
    }

    if (executedCorrectly) {
        const std::vector<std::string> interfaces = getDirectoryFileList("/sys/class/net/");
        std::vector<std::string> ifconfigWords;
        boost::split(ifconfigWords, ifconfig, boost::is_any_of("\t \n"), boost::token_compress_on);
        Json::Value hwAddrs;
        std::string currentInterface;
        bool hwAddrWordFlag = false;
        for (const auto& item : ifconfigWords) {
            if (hwAddrWordFlag) {
                hwAddrs[currentInterface] = item;
                hwAddrWordFlag = false;
            } else if (item == "HWaddr") {
                hwAddrWordFlag = true;
            } else if (find(interfaces.begin(), interfaces.end(), item) != interfaces.end()) {
                currentInterface = item;
            }
        }
        device_->telemetry()->reportEvent("hwAddrInfo", jsonToString(hwAddrs));
    }
}

/*
 * This is a copy of Java function from yandexstation, so they work similarly
 * Implementation copied from http://androidxref.com/6.0.1_r10/xref/frameworks/base/wifi/java/android/net/wifi/WifiManager.java
 */
int WifiEndpoint::calculateSignalLevel(int rssi, int numLevels) {
    constexpr int MIN_RSSI = -100;
    constexpr int MAX_RSSI = -50;
    if (rssi <= MIN_RSSI) {
        return 0;
    } else if (rssi >= MAX_RSSI) {
        return numLevels - 1;
    } else {
        float inputRange = (MAX_RSSI - MIN_RSSI);
        float outputRange = (numLevels - 1);
        return (int)((float)(rssi - MIN_RSSI) * outputRange / inputRange);
    }
}

/*
 * This is a copy of Java function from WifiManagerService.java, so they work similarly
 */
int WifiEndpoint::getChannel(int freq) {
    if (freq < 0) {
        return -1;
    }
    if (freq == 2484) {
        return 14;
    }
    if (freq < 2484) {
        return (freq - 2407) / 5;
    }
    return freq / 5 - 1000;
}
