#include "wifi_manager.h"

#include "wifi_utils.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/wpa_cli/wpa_client_supplicant.h>

#include <boost/algorithm/string.hpp>

#include <functional>
#include <iostream>

YIO_DEFINE_LOG_MODULE("wifi");

using namespace quasar;

const std::string& WifiManager::Action_Name(const Action& value)
{
    static std::string names[] = {
        "SUPPLICANT_STATE_CHANGED_ACTION",
        "NETWORK_STATE_CHANGED_ACTION",
        "CONNECTIVITY_ACTION",
        "<UNKNOWN>",
    };

    switch (value)
    {
        case Action::SUPPLICANT_STATE_CHANGED_ACTION:
            return names[0];
        case Action::NETWORK_STATE_CHANGED_ACTION:
            return names[1];
        case Action::CONNECTIVITY_ACTION:
            return names[2];
        default:
            return names[3];
    }
}

WifiManager::WifiManager(std::shared_ptr<YandexIO::IDevice> device, WpaClient& wpaClient)
    : device_(std::move(device))
    , wpa_(wpaClient)
{
    wpa_.setCallback(std::bind(&WifiManager::monitorEvent, this, std::placeholders::_1));
    wpa_.startThread();
}

WifiManager::~WifiManager()
{
    wpa_.clearCallback();
}

void WifiManager::sleepUntilWpaCliAttached()
{
    while (!isWifiEnabled())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool WifiManager::startScan()
{
    YIO_LOG_DEBUG("WifiManager::startScan");

    try {
        const std::string buf = wpa_.scan();
        return buf == "OK\n";
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedStartScan", e.what());
        return false;
    }
}

int WifiManager::addNetwork(const WifiConfiguration& config)
{
    /*
     * frameworks/opt/net/wifi/service/java/com/android/server/wifi/WifiConfigStore.java:addOrUpdateNetwork()
     */
    YIO_LOG_DEBUG("WifiManager::addNetwork");

    std::string buf;

    try {
        buf = wpa_.listNetworks();
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedListNetworks", e.what());
        return -1;
    }

    int networkId = -1;

    /* Check if the network already exists by SSID */
    // SSID is kept with quotes, so don't use first and last symbols
    const char* p = strstr(buf.c_str(), config.SSID.substr(1, config.SSID.length() - 2).c_str());
    if (p)
    {
        /* Seek to the beginning of the line */
        while (p != buf && *p != '\n') {
            p--;
        }
        if (p != buf)
        {
            /* Remove old network */
            networkId = atoi(p + 1);
            try {
                wpa_.disableNetwork(networkId);
                wpa_.removeNetwork(networkId);
                wpa_.saveConfig();
            } catch (const WpaClientException& e) {
                YIO_LOG_ERROR_EVENT("WifiManager.FailedRemoveOldNetwork", e.what());
                return -1;
            }
        }
    }

    try {
        buf = wpa_.addNetwork();
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedAddNetwork", e.what());
        return -1;
    }

    networkId = stoi(buf);
    if (networkId < 0) {
        return networkId;
    }

    try {
        // TODO: encodeSSID
        setNetworkVariable(networkId, "ssid", config.SSID);
        setNetworkVariable(networkId, "bssid", config.BSSID);
        setNetworkVariable(networkId, "key_mgmt", config.allowedKeyManagement, WifiUtils::keyMgmtStringMap);
        setNetworkVariable(networkId, "proto", config.allowedProtocols, WifiUtils::protocolStringMap);
        setNetworkVariable(networkId, "auth_alg", config.allowedAuthAlgorithms, WifiUtils::authAlgorithmStringMap);
        setNetworkVariable(networkId, "pairwise", config.allowedPairwiseCiphers, WifiUtils::pairwiseCipherStringMap);
        setNetworkVariable(networkId, "group", config.allowedGroupCiphers, WifiUtils::groupCipherStringMap);
        if (!config.preSharedKey.empty()) {
            setNetworkVariable(networkId, "psk", config.preSharedKey);
        }

        bool hasSetKey = false;
        for (unsigned int i = 0; i < config.wepKeys.size(); i++)
        {
            setNetworkVariable(networkId, (std::string("wep_key") + std::to_string(i)).c_str(), config.wepKeys.at(i));
            hasSetKey = true;
        }
        if (hasSetKey)
        {
            setNetworkVariable(networkId, "wep_tx_keyid", config.wepTxKeyIndex);
        }
        setNetworkVariable(networkId, "priority", config.priority);

        setNetworkVariable(networkId, "scan_ssid", config.hiddenSSID ? 1 : 0);

    } catch (std::runtime_error& e)
    {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedSetupNetwork", "Network setup fail: " << e.what());
        wpa_.removeNetwork(networkId);
        return -1;
    }

    try {
        YIO_LOG_INFO("Enabling network with ssid " << config.SSID);
        wpa_.enableNetwork(networkId);
        wpa_.saveConfig();
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedEnableNetwork", e.what());
        return -1;
    }

    return networkId;
}

bool WifiManager::removeNetwork(int netId)
{
    YIO_LOG_DEBUG("WifiManager::removeNetwork: " << netId);

    try {
        const std::string buf = wpa_.removeNetwork(netId);
        const bool is_ok = (buf == "OK\n");
        wpa_.saveConfig();
        return is_ok;
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedRemoveNetwork", e.what())
        return false;
    }
}

bool WifiManager::enableNetwork(int netId, bool flag)
{
    YIO_LOG_INFO("WifiManager::enableNetwork: "
                 << netId
                 << (flag ? " YES" : " NO")
                 << ", wpa_attached="
                 << wpa_.isAttached()
                 << ", wpa_state="
                 << getWpaState());

    std::string buf;
    if (flag)
    {
        try {
            buf = wpa_.enableNetwork(netId);
            if (buf != "OK\n") {
                YIO_LOG_ERROR_EVENT("WifiManager.FailedEnableNetwork", "WifiManager::enableNetwork: wpa_.enableNetwork failed: " << buf);
                return false;
            }

            return true;
        } catch (const WpaClientException& e) {
            YIO_LOG_ERROR_EVENT("WifiManager.FailedEnableNetwork", "WifiManager::enableNetwork: WpaClientException: " << e.what());
            return false;
        }
    } else {
        try {
            buf = wpa_.disableNetwork(netId);
            if (buf != "OK\n") {
                YIO_LOG_ERROR_EVENT("WifiManager.FailedDisableNetwork", "WifiManager::enableNetwork: wpa_.disableNetwork failed: " << buf);
                return false;
            }

            return true;
        } catch (const WpaClientException& e) {
            YIO_LOG_ERROR_EVENT("WifiManager.FailedDisableNetwork", "WifiManager::enableNetwork: WpaClientException: " << e.what());
            return false;
        }
    }
}

bool WifiManager::reloadConfiguration()
{
    YIO_LOG_DEBUG("WifiManager::reloadConfiguration");

    try {
        const std::string buf = wpa_.reloadConfig();
        return buf == "OK\n";
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedReloadConfig", e.what())
        return false;
    }
}

bool WifiManager::saveConfiguration()
{
    YIO_LOG_DEBUG("WifiManager::saveConfiguration");

    try {
        const std::string buf = wpa_.saveConfig();
        return buf == "OK\n";
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedSaveConfig", e.what())
        return false;
    }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
bool WifiManager::setWifiEnabled(bool flag)
{
    YIO_LOG_DEBUG("WifiManager::setWifiEnabled: " << (flag ? "YES" : "NO"));
#if ANDROID
    /*
     * TODO: use ServiceManager (example: frameworks/native/cmds/service/service.cpp)
     */
    return system((std::string("svc wifi ") + (flag ? "enable" : "disable")).c_str()) == 0;
#else
    /*
     * TODO: start-stop-daemon for wpa_supplicant
     */
    return true; // system((std::string("service wpa_supplicant ") + (flag ? "start" : "stop")).c_str());
#endif
}

bool WifiManager::isWifiEnabled()
{
    YIO_LOG_DEBUG("WifiManager::isWifiEnabled");

    return wpa_.isAttached();
}

std::string WifiManager::getWpaState() const {
    YIO_LOG_DEBUG("WifiManager::getWpaState");

    const auto status = getWpaStatus();

    const auto it = status.find("wpa_state");

    if (it != status.end()) {
        return it->second;
    }

    return "";
}

WifiManager::WpaStatus WifiManager::getWpaStatus() const {
    YIO_LOG_DEBUG("WifiManager::getWpaStatus");

    std::string statusRaw;

    try {
        statusRaw = wpa_.status(nullptr);
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedGetStatus", "WifiManager::getWpaStatus: exception: " << e.what());
        return {};
    }

    WpaStatus status;

    std::istringstream s(statusRaw);
    std::string line;

    while (std::getline(s, line)) {
        const size_t sep = line.find('=');

        if (sep == std::string::npos) {
            continue;
        }

        status[line.substr(0, sep)] = line.substr(sep + 1);
    }

    return status;
}

DetailedState WifiManager::getWifiState() const {
    YIO_LOG_INFO("WifiManager::getWifiState");

    const std::string wpa_state = getWpaState();

    YIO_LOG_INFO("WifiManager::getWifiState: wpa_state=" << wpa_state);

    if (!wpa_state.size()) {
        return DetailedState::DISCONNECTED;
    } else if ("COMPLETED" == wpa_state) {
        return DetailedState::CONNECTED;
    } else if ("DISCONNECTED" == wpa_state) {
        return DetailedState::DISCONNECTED;
    } else if ("INTERFACE_DISABLED" == wpa_state) {
        return DetailedState::DISCONNECTED;
    } else if ("INACTIVE" == wpa_state) {
        return DetailedState::DISCONNECTED;
    } else if ("SCANNING" == wpa_state) {
        return DetailedState::SCANNING;
    } else if ("AUTHENTICATING" == wpa_state) {
        return DetailedState::AUTHENTICATING;
    } else if ("ASSOCIATING" == wpa_state) {
        return DetailedState::CONNECTING;
    } else if ("ASSOCIATED" == wpa_state) {
        return DetailedState::AUTHENTICATING;
    } else if ("4WAY_HANDSHAKE" == wpa_state) {
        return DetailedState::AUTHENTICATING;
    } else if ("GROUP_HANDSHAKE" == wpa_state) {
        return DetailedState::AUTHENTICATING;
    }

    return DetailedState::DISCONNECTED;
}

bool WifiManager::interfaceDisabled() const {
    return getWpaState() == "INTERFACE_DISABLED";
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void WifiManager::enableInterface() const {
    std::ignore = std::system("/system/vendor/quasar/wireless_iface_up.sh");
}

std::vector<ScanResult> WifiManager::getScanResults()
{
    std::vector<ScanResult> scanResults;

    std::string buffer;
    try {
        buffer = wpa_.scanResults();
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedGetScanResults", e.what());
        return scanResults;
    }
    YIO_LOG_DEBUG("buffer: " << buffer);

    std::stringstream buf(buffer);
    /* eat first line with header */
    std::string line;
    std::getline(buf, line);

    while (std::getline(buf, line))
    {
        ScanResult res;
        std::vector<std::string> items;
        boost::iter_split(items, line, boost::first_finder("\t"));
        if (items.size() < 5) {
            continue;
        }

        res.BSSID = items[0];
        res.rate = atoi(items[1].c_str());
        res.rssi = atoi(items[2].c_str());
        res.capabilities = items[3];
        res.SSID = unescapeSSID(items[4]);

        scanResults.push_back(res);
    }

    return scanResults;
}

std::string WifiManager::unescapeSSID(const std::string& escapedSSID)
{
    std::string result;

    for (size_t i = 0; i < escapedSSID.length(); ++i)
    {
        // Abridged version of https://github.com/digsrc/wpa_supplicant/blob/515eb37dd1df3f4a05fc2a31c265db6358301988/src/utils/common.c#L466
        if (i + 1 < escapedSSID.length() && '\\' == escapedSSID[i] && '\\' == escapedSSID[i + 1])
        {
            result += '\\';
            ++i;
        } else if (i + 1 < escapedSSID.length() && '\\' == escapedSSID[i] && '\"' == escapedSSID[i + 1]) {
            result += '\"';
            ++i;
        } else if (i + 3 < escapedSSID.length() && '\\' == escapedSSID[i] && 'x' == escapedSSID[i + 1]) {
            char hex[3] = {escapedSSID[i + 2], escapedSSID[i + 3], '\0'};
            char byte = static_cast<char>(strtol(hex, nullptr, 16));
            result += byte;
            i += 3;
        } else {
            result += escapedSSID[i];
        }
    }

    return result;
}

std::map<int, WifiConfiguration> WifiManager::getConfiguredNetworks()
{
    /*
     * frameworks/opt/net/wifi/service/java/com/android/server/wifi/WifiConfigStore.java
     */
    YIO_LOG_DEBUG("WifiManager::getConfiguredNetworks");

    std::map<int, WifiConfiguration> networks;
    std::string buffer;
    try {
        buffer = wpa_.listNetworks();
    } catch (const WpaClientException& e) {
        device_->telemetry()->reportError("listNetworksFailed");
        YIO_LOG_ERROR_EVENT("WifiManager.FailedListNetworks", e.what());
        std::lock_guard<std::mutex> g(lock_);
        currentNetwork_.reset();
        return networks;
    }

    std::stringstream buf(buffer);
    /* eat first line with header */
    std::string line;
    std::getline(buf, line);

    bool hasCurrentNetwork = false;

    while (std::getline(buf, line))
    {
        std::vector<std::string> items;
        boost::iter_split(items, line, boost::first_finder("\t"));
        if (items.empty()) {
            continue;
        }

        auto config = readNetworkVariables(atoi(items[0].c_str()));
        /*
         * TODO:
         * extras = mWifiNative.getNetworkExtra(config.networkId, ID_STRING_VAR_NAME);
         *
         * config.setIpAssignment(IpAssignment.DHCP);
         * config.setProxySettings(ProxySettings.NONE);
         * configKey = extras.get(ID_STRING_KEY_CONFIG_KEY);
         * if (configKey == null)
         *     saveNetworkMetadata(config);
         */

        auto flag = items.back();
        if (flag.find("[CURRENT]") != std::string::npos)
        {
            setWifiSignal(&config);
            hasCurrentNetwork = true;
            std::lock_guard<std::mutex> g(lock_);
            currentNetwork_ = std::make_shared<WifiConfiguration>(config);
        }

        if (config.networkId != -1)
        {
            auto e = networks.find(config.networkId);
            if (e != networks.end()) {
                e->second = config;
            } else {
                networks.emplace(config.networkId, config);
            }
        }
    }

    return networks;
}

template <typename T, typename M>
void WifiManager::setNetworkVariable(int netId, const char* varname, const T& vec, const M& map)
{
    std::string val;

    for (auto& x : vec)
    {
        auto e = map.find(x);
        if (e != map.end())
        {
            if (!val.empty()) {
                val += " ";
            }
            val += e->second;
        }
    }

    if (!val.empty()) {
        setNetworkVariable(netId, varname, val);
    }
}

void WifiManager::setNetworkVariable(int netId, const char* varname, const std::string& value)
{
    std::string buf;
    try {
        buf = wpa_.setNetwork(netId, varname, value.c_str());
    } catch (const WpaClientException& e) {
        throw std::runtime_error(std::string("Unable to set variable ") + varname + " to [" + value + "]");
    }
    if (buf != "OK\n") {
        throw std::runtime_error(std::string("setNetworkVariable failed: [") + buf + "]");
    }
}

bool WifiManager::setWifiSignal(WifiConfiguration* network) const {
    std::string buffer;
    try {
        buffer = wpa_.signalPoll();
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedSignalPoll", e.what())
        return false;
    }

    std::istringstream iss(buffer);

    std::string key;
    std::string val;
    while (std::getline(std::getline(iss, key, '=') >> std::ws, val))
    {
        if ("RSSI" == key) {
            network->rssi = atoi(val.c_str());
        } else if ("FREQUENCY" == key) {
            network->frequency = atoi(val.c_str());
        } else if ("LINKSPEED" == key) {
            network->linkspeed = atoi(val.c_str());
        }
    }

    return true;
}

std::string WifiManager::getNetworkVariable(int netId, const char* varname)
{
    std::string dst;
    std::string buf;
    try {
        buf = wpa_.getNetwork(netId, varname);
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedGetNetwork", e.what())
        return "";
    }

    if (!strncmp(buf.c_str(), "FAIL", 4)) {
        return "";
    }

    return buf;
}

void WifiManager::setNetworkVariable(int netId, const char* varname, const int value)
{
    setNetworkVariable(netId, varname, std::to_string(value));
}

template <typename M>
auto WifiManager::getNetworkVariable(int netId, const char* /* varname */, const M& map) -> std::vector<typename M::mapped_type> {
    std::string::size_type b = 0;
    std::vector<typename M::mapped_type> vec;
    auto val = getNetworkVariable(netId, "proto");
    while ((b = val.find_first_not_of(' ', b)) != std::string::npos)
    {
        auto e = val.find_first_of(' ', b);
        auto item = map.find(val.substr(b, e - b));
        if (item != map.end()) {
            vec.push_back(item->second);
        }
        b = e;
    }
    return vec;
}

WifiConfiguration WifiManager::readNetworkVariables(int networkId)
{
    WifiConfiguration config;

    config.networkId = networkId;

    config.SSID = getNetworkVariable(config.networkId, "ssid");
    if (config.SSID[0] != '"')
    {
        config.SSID = std::string("\"") + config.SSID /* WifiSsid.createFromHex(value) */ + "\"";
    }
    // setNetworkSelectionBSSID
    config.BSSID = getNetworkVariable(config.networkId, "bssid");

    auto val = getNetworkVariable(config.networkId, "priority");
    config.priority = val.empty() ? -1 : atoi(val.c_str());

    val = getNetworkVariable(config.networkId, "scan_ssid");
    config.hiddenSSID = !val.empty() && atoi(val.c_str()) != 0;

    val = getNetworkVariable(config.networkId, "wep_tx_keyidx");
    config.wepTxKeyIndex = val.empty() ? -1 : atoi(val.c_str());

    for (int i = 0; i < 4; i++)
    {
        val = getNetworkVariable(config.networkId, ("wep_key" + std::to_string(i)).c_str());
        if (!val.empty()) {
            config.wepKeys.insert({i, val});
        }
    }

    config.preSharedKey = getNetworkVariable(config.networkId, "psk");

    config.allowedProtocols = getNetworkVariable(config.networkId, "proto", WifiUtils::protocolMap);
    config.allowedKeyManagement = getNetworkVariable(config.networkId, "key_mgmt", WifiUtils::keyMgmtMap);
    config.allowedAuthAlgorithms = getNetworkVariable(config.networkId, "auth_alg", WifiUtils::authAlgorithmMap);
    config.allowedPairwiseCiphers = getNetworkVariable(config.networkId, "pairwise", WifiUtils::pairwiseCipherMap);
    config.allowedGroupCiphers = getNetworkVariable(config.networkId, "group", WifiUtils::groupCipherMap);

    return config;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
proto::WifiStatus::Signal WifiManager::calculateSignalLevel(int rssi, int numLevels)
{
    int MIN_RSSI = -100;
    int MAX_RSSI = -55;

    YIO_LOG_DEBUG("WifiManager::calculateSignalLevel: " << rssi);

    if (rssi <= MIN_RSSI) {
        return proto::WifiStatus::WEAK;
    } else if (rssi >= MAX_RSSI) {
        return static_cast<proto::WifiStatus::Signal>(numLevels - 1);
    }

    float inputRange = (MAX_RSSI - MIN_RSSI);
    float outputRange = (numLevels - 1);
    int level = (int)((float)(rssi - MIN_RSSI) * outputRange / inputRange);

    return static_cast<proto::WifiStatus::Signal>(level);
}

std::shared_ptr<const WifiConfiguration> WifiManager::getConnectionInfo() const {
    std::lock_guard<std::mutex> g(lock_);

    const auto status = getWpaStatus();
    WifiConfiguration configuration;
    try {
        configuration.BSSID = status.at("bssid");
        configuration.SSID = status.at("ssid");
        configuration.frequency = std::stoi(status.at("freq"));
        configuration.networkId = std::stoi(status.at("id"));
    } catch (const std::out_of_range& e) {
        YIO_LOG_DEBUG("bssid, ssid, freq or id field not found in wpa status");
        return nullptr;
    }

    setWifiSignal(&configuration);

    return std::make_shared<const WifiConfiguration>(configuration);
}

std::unordered_map<std::string, std::string> WifiManager::parseReason(const char* buf)
{
    const char* p = buf;
    const char* ctl_p = buf;
    bool is_key = true;
    bool has_quote = false;
    std::string key;
    std::string item;

    std::unordered_map<std::string, std::string> info;

    info.clear();
    if (!buf || !*buf) {
        return info;
    }

    /* Seek to first argument */
    while (*p && (*p != ' ' && *p != '\t' && *p != '\n'))
    {
        if (*p == '>') {
            ctl_p = p + 1;
        }
        p++;
    }

    item.assign(ctl_p, p - ctl_p);
    info.emplace("ctrl", item);

    if (!*p || *p == '\n') {
        return info;
    }
    p++;

    while (*p)
    {
        const char* item_p = p;
        bool seek_next_key = false;
        do {
            if (is_key)
            {
                if (*item_p == '=') {
                    break;
                } else if (*item_p == ' ')
                {
                    p++;
                    seek_next_key = true;
                    break;
                }
            } else if (!is_key)
            {
                if (has_quote && *item_p == '"')
                {
                    item_p++;
                    has_quote = false;
                    break;
                } else if (*item_p == '"')
                {
                    has_quote = true;
                } else if (*item_p == ' ' || *item_p == '\t' || *item_p == '\n')
                {
                    break;
                }
            }
            item_p++;
        } while (*item_p);

        if (seek_next_key) {
            continue;
        }

        if (item_p != p)
        {
            item.assign(p, item_p - p);
            p = item_p;
        }
        while (*p && (*p == '\t' || *p == ' ' || *p == '=')) {
            p++;
        }

        if (is_key)
        {
            key.assign(item);
        } else {
            info.emplace(key, item);
            key.clear();
            item.clear();
        }

        is_key = !is_key;

        if (!is_key && *p == '\n') {
            break;
        }
    }

    return info;
}

void WifiManager::deregisterCallbacks()
{
    callbackResults_ = nullptr;
    callbackAction_ = nullptr;
}

void WifiManager::monitorEvent(const char* buf)
{
    std::unordered_map<std::string, std::string> info;
    std::vector<ScanResult> scanResults;

    YIO_LOG_INFO("Received monitorEvent:  " << std::string(buf));
    if (strstr(buf, ">CTRL-EVENT-SCAN-RESULTS"))
    {
        if (callbackResults_)
        {
            scanResults = getScanResults();
            if (!scanResultsReceived_ && !scanResults.empty())
            {
                YIO_LOG_INFO("Received " << scanResults.size() << " scan results");
                scanResultsReceived_ = true;
            }
            if (scanResultsReceived_ && scanResults.empty())
            {
                YIO_LOG_INFO("Received empty scan results");
                scanResultsReceived_ = false;
            }
            callbackResults_(scanResults);
            return;
        }
    } else if (strstr(buf, ">CTRL-EVENT-SUBNET-STATUS-UPDATE"))
    {
        YIO_LOG_INFO("Received " << buf);
        if (callbackAction_)
        {
            info = parseReason(buf);
            callbackAction_(Action::NETWORK_STATE_CHANGED_ACTION, info);
            return;
        }
    } else if (strstr(buf, ">CTRL-EVENT-ASSOC-REJECT"))
    {
        YIO_LOG_INFO("Received " << buf);
        if (callbackAction_)
        {
            info = parseReason(buf);
            callbackAction_(Action::NETWORK_STATE_CHANGED_ACTION, info);
            return;
        }
    } else if (strstr(buf, ">CTRL-EVENT-SSID-TEMP-DISABLED"))
    {
        YIO_LOG_INFO("Received " << buf);
        if (callbackAction_)
        {
            info = parseReason(buf);
            callbackAction_(Action::NETWORK_STATE_CHANGED_ACTION, info);
            return;
        }
    } else if (strstr(buf, ">CTRL-EVENT-CONNECTED"))
    {
        YIO_LOG_INFO("Received " << buf);
        if (callbackAction_)
        {
            info = parseReason(buf);
            callbackAction_(Action::SUPPLICANT_STATE_CHANGED_ACTION, info);
            return;
        }
    } else if (strstr(buf, ">CTRL-EVENT-DISCONNECTED"))
    {
        YIO_LOG_INFO("Received " << buf);
        if (callbackAction_)
        {
            info = parseReason(buf);
            callbackAction_(Action::SUPPLICANT_STATE_CHANGED_ACTION, info);
            return;
        }
    } else if (strstr(buf, ">CTRL-EVENT-TERMINATING"))
    {
        YIO_LOG_INFO("Received " << buf);
        if (callbackAction_)
        {
            info = parseReason(buf);
            callbackAction_(Action::SUPPLICANT_STATE_CHANGED_ACTION, info);
            return;
        }
    }
}

bool WifiManager::reconnect() {
    YIO_LOG_DEBUG("WifiManager::reconnect");

    try {
        const std::string buf = wpa_.reconnect();
        const bool is_ok = (buf == "OK\n");
        return is_ok;
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedReconnect", e.what())
        return false;
    }
}

bool WifiManager::disconnect() {
    YIO_LOG_DEBUG("WifiManager::disconnect");

    try {
        const std::string buf = wpa_.disconnect();
        const bool is_ok = (buf == "OK\n");
        return is_ok;
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedDisconnect", e.what())
        return false;
    }
}

bool WifiManager::reassociate() {
    YIO_LOG_DEBUG("WifiManager::reassociate");

    try {
        const std::string buf = wpa_.reassociate();
        const bool is_ok = (buf == "OK\n");
        return is_ok;
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedReassociate", e.what())
        return false;
    }
}

bool WifiManager::enableAllNetworks() {
    YIO_LOG_DEBUG("WifiManager::enableAllNetworks");
    try {
        const std::string buf = wpa_.enableAllNetworks();
        const bool is_ok = (buf == "OK\n");
        return is_ok;
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedEnableAllNetworks", e.what())
        return false;
    }
}

bool WifiManager::disableAllNetworks() {
    YIO_LOG_DEBUG("WifiManager::disableAllNetworks");
    try {
        const std::string buf = wpa_.disableAllNetworks();
        const bool is_ok = (buf == "OK\n");
        return is_ok;
    } catch (const WpaClientException& e) {
        YIO_LOG_ERROR_EVENT("WifiManager.FailedDisableAllNetworks", e.what())
        return false;
    }
}
