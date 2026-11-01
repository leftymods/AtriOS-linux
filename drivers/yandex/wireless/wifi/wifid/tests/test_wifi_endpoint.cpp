#include <yandex_io/services/wifid/wifi_utils.h>
#include <yandex_io/services/wifid/testlib/test_wpa_client.h>

#include <yandex_io/interfaces/device_state/mock/device_state_provider.h>
#include <yandex_io/interfaces/user_config/mock/user_config_provider.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_connector.h>
#include <yandex_io/libs/ipc/i_server.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/tests/testlib/test_utils.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <library/cpp/testing/unittest/registar.h>

#include <future>
#include <map>

using namespace quasar;
using namespace quasar::TestUtils;
using namespace std::chrono;

#define SCAN_HEADER "BSSID\trate\tlevel\tcaps\tSSID\n"
#define LIST_HEADER "network id / ssid / bssid / flags\n"

namespace {
    struct Network {
        inline std::string scan_result() const {
            return BSSID + "\t" + std::to_string(rate) + "\t" + std::to_string(level) + "\t" + capabilities + "\t" + escapedSSID + "\n";
        }

        inline std::string list(int currentNetwork) const {
            std::string ssid;
            if (SSID[0] != '"') {
                ssid = SSID;
            } else {
                ssid = SSID.substr(1, SSID.length() - 2);
            }
            std::string buf = std::to_string(networkId) + "\t" + ssid + "\t" + BSSID;
            if (networkId == currentNetwork) {
                buf += "\t[CURRENT]\n";
            } else {
                buf += "\n";
            }
            return buf;
        }

        int networkId;
        std::string SSID;
        std::string escapedSSID;
        std::string BSSID;
        std::string capabilities;
        int level;
        int rate;
        int speed;
        bool disabled;
        std::string password;
        std::string proto;
    };

    struct BaseFixture: public QuasarUnitTestFixture {
    public:
        using Base = QuasarUnitTestFixture;

        void SetUp(NUnitTest::TTestContext& context) override {
            Base::SetUp(context);

            auto& config = getDeviceForTests()->configuration()->getMutableConfig(testGuard);
            auto& serviceConfig = config[WifiEndpoint::SERVICE_NAME];

            deviceStateProvider = std::make_shared<mock::DeviceStateProvider>();
            userConfigProvider = std::make_shared<mock::UserConfigProvider>(nullptr);

            serviceConfig["accessPointStartTimeoutSec"] = "100";
            serviceConfig["connectivityStateChangeTimeoutSeconds"] = 5;
            serviceConfig["waitForInternetAfterConnectSec"] = 1;

            {
                std::lock_guard<std::mutex> g(lock);

                lastCommand = TestWpaClient::IS_ATTACHED;
                scanResults.assign(SCAN_HEADER);

                discoveredNetworkList = {
                    {"TEST", {-1, "TEST", "TEST", "00:11:22:33:44:55", "[WEP-PSK-CCMP][ESS]", -28, 2440, 65, true, "p@ssw0rd", "WEP"}},
                    {"Кириллица, лол", {-1, "Кириллица, лол", "\\xd0\\x9a\\xd0\\xb8\\xd1\\x80\\xd0\\xb8\\xd0\\xbb\\xd0\\xbb\\xd0\\xb8\\xd1\\x86\\xd0\\xb0, \\xd0\\xbb\\xd0\\xbe\\xd0\\xbb", "00:11:22:33:44:55", "[ESS]", -29, 2440, 65, true, "p@ssw0rd", "OPEN"}},
                    {"x\\x", {-1, "x\\x", "x\\\\x", "00:11:22:33:44:55", "[WPA2-PSK-CCMP][ESS]", -30, 2440, 65, true, "p@ssw0rd", "WPA"}},
                };

                signalPoll = {
                    {"RSSI", -9999},
                    {"LINKSPEED", 0},
                    {"NOISE", 9999},
                    {"FREQUENCY", 2437},
                };
            }

            testWpaClient.onCommand(TestWpaClient::SCAN, [&](std::string& buf) {
                {
                    std::lock_guard<std::mutex> g(lock);

                    lastCommand = TestWpaClient::SCAN;
                    scanResults.assign(SCAN_HEADER);
                    buf.assign("OK\n");
                }

                testWpaClient.scheduleMonitorEvent(5, [&](std::string& buf) {
                    buf.assign(">CTRL-EVENT-SCAN-STARTED\n");
                });
                testWpaClient.scheduleMonitorEvent(10, [&](std::string& buf) {
                    std::lock_guard<std::mutex> g(lock);

                    buf.assign(">CTRL-EVENT-SCAN-RESULTS\n");

                    scanResults.assign(SCAN_HEADER);
                    for (auto& n : discoveredNetworkList) {
                        scanResults += n.second.scan_result();
                    }

                    hasScanResults.set_value(true);
                });
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::SCAN_RESULTS, [&](std::string& buf) {
                std::lock_guard<std::mutex> g(lock);

                lastCommand = TestWpaClient::SCAN_RESULTS;
                buf.assign(scanResults);
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::LIST_NETWORK, [&](std::string& buf) {
                std::lock_guard<std::mutex> g(lock);

                lastCommand = TestWpaClient::LIST_NETWORK;
                buf.assign(LIST_HEADER);
                for (auto& n : networkList)
                {
                    buf += n.second.list(currentNetwork);
                }
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::DISABLE_NETWORK, [&](std::string& buf, int netId) {
                std::lock_guard<std::mutex> g(lock);

                auto net = networkList.find(netId);
                if (net == networkList.end()) {
                    buf.assign("FAIL\n");
                    return false;
                }
                net->second.disabled = true;
                if (currentNetwork == netId) {
                    currentNetwork = -1;
                }
                buf.assign("OK\n");
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::DISABLE_ALL_NETWORKS, [&](std::string& buf, int /* netId */) {
                std::lock_guard<std::mutex> g(lock);
                for (auto& net : networkList) {
                    net.second.disabled = true;
                }
                currentNetwork = -1;
                buf.assign("OK\n");
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::ENABLE_NETWORK, [&](std::string& buf, int netId) {
                std::lock_guard<std::mutex> g(lock);

                auto net = networkList.find(netId);
                if (net == networkList.end()) {
                    buf.assign("FAIL\n");
                    return false;
                }
                net->second.disabled = false;
                buf.assign("OK\n");
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::ENABLE_ALL_NETWORKS, [&](std::string& buf, int /* netId */) {
                std::lock_guard<std::mutex> g(lock);
                for (auto& net : networkList) {
                    net.second.disabled = false;
                }
                buf.assign("OK\n");
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::REASSOCIATE, [&](std::string& buf) {
                // REASSOCIATE command from wpa_cli - currently wpa_supplicant issues a scan request, finds a network with highest RSSI and associates with it, we're trying to imitate this behaviour
                std::lock_guard<std::mutex> g(lock);
                YIO_LOG_INFO("Reassociate started");
                const auto net = std::max_element(networkList.begin(), networkList.end(), [](const decltype(networkList)::value_type& a, const decltype(networkList)::value_type& b) { return a.second.level > b.second.level; });
                if (net == networkList.end()) {
                    YIO_LOG_INFO("No network found, unable to reassociate");
                    testWpaClient.scheduleMonitorEvent(5, [&](std::string& buf) {
                        buf.assign(">CTRL-EVENT-DISCONNECTED\n");
                        wpaState = DetailedState::DISCONNECTED;
                        signalPoll["RSSI"] = -9999;
                        wifiDisconnected.set_value(true);
                    });
                } else {
                    YIO_LOG_INFO("Associating with network SSID: " << net->second.SSID << ", RSSI: " << net->second.level);
                    testWpaClient.scheduleMonitorEvent(5, [=](std::string& buf) {
                        const auto apNet = discoveredNetworkList.find(net->second.SSID.substr(1, net->second.SSID.length() - 2));
                        if (apNet == discoveredNetworkList.end()) {
                            buf.assign(">CTRL-EVENT-ASSOC-REJECT\n");
                            wpaState = DetailedState::DISCONNECTED;
                            signalPoll["RSSI"] = -9999;
                            wifiDisconnected.set_value(true);
                            return;
                        }
                        if (apNet->second.password != net->second.password) {
                            wpaState = DetailedState::CONNECTED;
                            buf.assign(">CTRL-EVENT-SSID-TEMP-DISABLED reason=WRONG_KEY\n");
                            signalPoll["RSSI"] = -9999;
                            wifiDisconnected.set_value(true);
                            return;
                        }
                        wpaState = DetailedState::CONNECTED;
                        buf.assign(">CTRL-EVENT-CONNECTED " + net->second.SSID + "\n");
                        auto discovered = discoveredNetworkList.find(net->second.SSID.substr(1, net->second.SSID.length() - 2));
                        if (discovered != discoveredNetworkList.end()) {
                            signalPoll["RSSI"] = discovered->second.level;
                        } else {
                            signalPoll["RSSI"] = -9999;
                        }
                        currentNetwork = net->first;
                        wifiConnected.set_value(true);
                    });
                    YIO_LOG_INFO("Reassociate succeeded");
                }
                buf.assign("OK\n");
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::SELECT_NETWORK, [&](std::string& buf, int netId) {
                {
                    std::lock_guard<std::mutex> g(lock);

                    auto net = networkList.find(netId);
                    if (net == networkList.end()) {
                        buf.assign("FAIL\n");
                        return false;
                    }
                    currentNetwork = netId;
                    wpaState = DetailedState::CONNECTING;

                    buf.assign("OK\n");
                }

                testWpaClient.scheduleMonitorEvent(10, [&](std::string& buf) {
                    std::lock_guard<std::mutex> g(lock);

                    auto net = networkList.find(currentNetwork);
                    if (net == networkList.end()) {
                        buf.assign(">CTRL-EVENT-DISCONNECTED\n");
                        wpaState = DetailedState::DISCONNECTED;
                        signalPoll["RSSI"] = -9999;
                        wifiDisconnected.set_value(true);
                        return;
                    }
                    auto apNet = discoveredNetworkList.find(net->second.SSID.substr(1, net->second.SSID.length() - 2));
                    if (apNet == discoveredNetworkList.end()) {
                        buf.assign(">CTRL-EVENT-ASSOC-REJECT\n");
                        wpaState = DetailedState::DISCONNECTED;
                        signalPoll["RSSI"] = -9999;
                        wifiDisconnected.set_value(true);
                        return;
                    }
                    if (apNet->second.password != net->second.password) {
                        wpaState = DetailedState::CONNECTED;
                        buf.assign(">CTRL-EVENT-SSID-TEMP-DISABLED reason=WRONG_KEY\n");
                        signalPoll["RSSI"] = -9999;
                        wifiDisconnected.set_value(true);
                        return;
                    }
                    wpaState = DetailedState::CONNECTED;
                    buf.assign(">CTRL-EVENT-CONNECTED " + net->second.SSID + "\n");
                    auto discovered = discoveredNetworkList.find(net->second.SSID.substr(1, net->second.SSID.length() - 2));
                    if (discovered != discoveredNetworkList.end()) {
                        signalPoll["RSSI"] = discovered->second.level;
                    } else {
                        signalPoll["RSSI"] = -9999;
                    }
                    wifiConnected.set_value(true);
                });

                return true;
            });

            testWpaClient.onCommand(TestWpaClient::SAVE, [&](std::string& /* buf */) {
                // just noop
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::IS_ATTACHED, [&](std::string& /* buf */) {
                // just noop
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::SIGNAL_POLL, [&](std::string& buf) {
                std::lock_guard<std::mutex> g(lock);

                auto net = networkList.find(currentNetwork);
                if (net == networkList.end()) {
                    // reset to defaults
                    signalPoll["RSSI"] = -9999;
                }
                buf.assign("");
                for (auto& kv : signalPoll)
                {
                    buf += kv.first + "=" + std::to_string(kv.second) + "\n";
                }
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::STATUS, [&](std::string& buf, const char* /* status */) {
                std::lock_guard<std::mutex> g(lock);

                switch (wpaState)
                {
                    case DetailedState::CONNECTED:
                        buf.assign("wpa_state=COMPLETED\n");
                        break;
                    case DetailedState::DISCONNECTED:
                        buf.assign("wpa_state=DISCONNECTED\n");
                        break;
                    case DetailedState::SCANNING:
                        buf.assign("wpa_state=SCANNING\n");
                        break;
                    case DetailedState::AUTHENTICATING:
                        buf.assign("wpa_state=AUTHENTICATING\n");
                        break;
                    case DetailedState::CONNECTING:
                        buf.assign("wpa_state=ASSOCIATING\n");
                        break;

                    default:
                        buf.assign("");
                        break;
                }

                auto net = networkList.find(currentNetwork);
                if (net != networkList.end()) {
                    buf.append("id=").append(std::to_string(net->second.networkId)).append("\n");
                    buf.append("freq=").append(std::to_string(net->second.rate)).append("\n");
                    buf.append("bssid=").append(net->second.BSSID).append("\n");
                    buf.append("ssid=").append(net->second.SSID).append("\n");
                }
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::REMOVE_NETWORK, [&](std::string& buf, int netId) {
                std::lock_guard<std::mutex> g(lock);

                auto net = networkList.find(netId);
                if (net == networkList.end()) {
                    buf.assign("FAIL\n");
                    return false;
                }
                networkList.erase(net);
                buf.assign("OK\n");
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::ADD_NETWORK, [&](std::string& buf) {
                std::lock_guard<std::mutex> g(lock);

                networkList.emplace(networkIdLast, Network{networkIdLast, "", "", "any", "", -9999, 0, 0, true, "", ""});
                buf.assign(std::to_string(networkIdLast) + "\n");
                networkIdLast++;
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::GET_NETWORK, [&](std::string& buf, int netId, const char* varname) {
                std::lock_guard<std::mutex> g(lock);

                lastCommand = TestWpaClient::GET_NETWORK;
                auto net = networkList.find(netId);
                if (net == networkList.end()) {
                    return false;
                }

                if (!strcmp("ssid", varname)) {
                    buf.assign(net->second.SSID);
                } else if (!strcmp("bssid", varname)) {
                    buf.assign(net->second.BSSID);
                } else if (!strcmp("proto", varname)) {
                    buf.assign(net->second.proto);
                } else {
                    buf.assign("FAIL\n");
                    return false;
                }
                return true;
            });

            testWpaClient.onCommand(TestWpaClient::SET_NETWORK, [&](std::string& buf, int netId, const char* varname, const char* value) {
                std::lock_guard<std::mutex> g(lock);

                lastCommand = TestWpaClient::SET_NETWORK;
                auto net = networkList.find(netId);
                if (net == networkList.end()) {
                    return false;
                }

                buf.assign("OK\n");
                if (!strcmp("ssid", varname)) {
                    net->second.SSID.assign(value);
                } else if (!strcmp("bssid", varname)) {
                    net->second.BSSID.assign(value);
                } else if (!strcmp("proto", varname)) {
                    net->second.proto.assign(value);
                } else if (!strcmp("wep_key0", varname)) {
                    std::string val(value);
                    net->second.password.assign(val.substr(1, val.length() - 2));
                } else {
                    buf.assign("OK\n");
                    return true;
                }
                return true;
            });
        }

        void TearDown(NUnitTest::TTestContext& context) override {
            Base::TearDown(context);
        }

        std::mutex lock;

        int currentNetwork = -1;
        int networkIdLast = 0;
        DetailedState wpaState = DetailedState::DISCONNECTED;

        std::map<int, Network> networkList;
        std::map<std::string, Network> discoveredNetworkList;
        std::map<std::string, int> signalPoll;
        std::string scanResults;
        std::promise<bool> hasScanResults;
        std::promise<bool> wifiConnected;
        std::promise<bool> wifiDisconnected;
        TestWpaClient::CmdType lastCommand;
        TestWpaClient testWpaClient;
        YandexIO::Configuration::TestGuard testGuard;

        std::shared_ptr<IDeviceStateProvider> deviceStateProvider;
        std::shared_ptr<IUserConfigProvider> userConfigProvider;
    };

    struct Fixture: public BaseFixture {
        using Base = BaseFixture;

        void SetUp(NUnitTest::TTestContext& context) override {
            Base::SetUp(context);

            const auto device = getDeviceForTests();
            wifiManager = std::make_unique<WifiManager>(device, testWpaClient);
            wifiEndpoint = std::make_unique<WifiEndpoint>(device, ipcFactoryForTests(), deviceStateProvider, userConfigProvider, *wifiManager);
        }

        void TearDown(NUnitTest::TTestContext& context) override {
            wifiEndpoint.reset();
            wifiManager.reset();

            Base::TearDown(context);
        }

        std::unique_ptr<WifiManager> wifiManager;
        std::unique_ptr<WifiEndpoint> wifiEndpoint;
    };

} // namespace

Y_UNIT_TEST_SUITE_F(WifiEndpointTests, Fixture) {
    Y_UNIT_TEST(testWifiList)
    {
        auto connector = createIpcConnectorForTests("wifid");
        connector->connectToService();
        connector->waitUntilConnected();

        hasScanResults.get_future().get();

        ipc::SharedMessage response;
        waitUntil([&]() {
            proto::QuasarMessage request;
            request.mutable_wifi_list_request();
            response = connector->sendRequestSync(proto::QuasarMessage{request}, std::chrono::seconds(5));

            return response->wifi_list().hotspots_size() != 0;
        });

        UNIT_ASSERT_VALUES_EQUAL(response->wifi_list().hotspots_size(), 3);

        proto::WifiInfo res;
        res.set_ssid("TEST");
        res.set_mac("00:11:22:33:44:55");
        res.set_secure(true);
        res.set_rssi(-28);

        UNIT_ASSERT(response->wifi_list().hotspots(0) == res);

        res.set_ssid("Кириллица, лол");
        res.set_secure(false);
        res.set_rssi(-29);
        UNIT_ASSERT(response->wifi_list().hotspots(1) == res);

        res.set_ssid("x\\x");
        res.set_secure(true);
        res.set_rssi(-30);
        UNIT_ASSERT(response->wifi_list().hotspots(2) == res);
    }

    Y_UNIT_TEST(testWifiConnect)
    {
        auto connector = createIpcConnectorForTests("wifid");
        connector->connectToService();
        connector->waitUntilConnected();

        hasScanResults.get_future().get();
        proto::QuasarMessage request;
        auto wifiConnect = request.mutable_wifi_connect();
        wifiConnect->set_wifi_id("TEST");
        wifiConnect->set_password("p@ssw0rd");
        auto response = connector->sendRequestSync(proto::QuasarMessage{request}, std::chrono::seconds(5));

        UNIT_ASSERT_EQUAL(response->wifi_connect_response().status(), proto::WifiConnectResponse::OK);
        UNIT_ASSERT_VALUES_EQUAL(wifiManager->getConnectionInfo()->rssi, -28);
    }

    Y_UNIT_TEST(testWifiConnectWrongPassword)
    {
        auto connector = createIpcConnectorForTests("wifid");
        connector->connectToService();
        connector->waitUntilConnected();

        hasScanResults.get_future().get();
        proto::QuasarMessage request;
        auto wifiConnect = request.mutable_wifi_connect();
        wifiConnect->set_wifi_id("TEST");
        wifiConnect->set_password("wrong-pass");
        auto response = connector->sendRequestSync(proto::QuasarMessage{request}, std::chrono::seconds(5));

        wifiDisconnected.get_future().get();

        UNIT_ASSERT_EQUAL(response->wifi_connect_response().status(), proto::WifiConnectResponse::AUTH_ERROR);
    }

    Y_UNIT_TEST(testAsyncWifiConnect)
    {
        auto connector = createIpcConnectorForTests("wifid");
        connector->connectToService();
        connector->waitUntilConnected();

        hasScanResults.get_future().get();

        proto::QuasarMessage request;
        auto wifiConnect = request.mutable_async_wifi_connect();
        wifiConnect->set_wifi_id("TEST");
        wifiConnect->set_password("p@ssw0rd");
        connector->sendMessage(proto::QuasarMessage{request});

        wifiConnected.get_future().get();
    }

    Y_UNIT_TEST(testAsyncWifiConnectWrongPassword)
    {
        auto connector = createIpcConnectorForTests("wifid");
        connector->connectToService();
        connector->waitUntilConnected();

        hasScanResults.get_future().get();

        proto::QuasarMessage request;
        auto wifiConnect = request.mutable_async_wifi_connect();
        wifiConnect->set_wifi_id("TEST");
        wifiConnect->set_password("wrong-pass");
        connector->sendMessage(proto::QuasarMessage{request});

        wifiDisconnected.get_future().get();
    }
}

Y_UNIT_TEST_SUITE_F(WifiAutoTests, QuasarUnitTestFixture) {
    Y_UNIT_TEST(testWifiUtilsParseType)
    {
        UNIT_ASSERT_EQUAL(WifiUtils::parseType(""), proto::WifiType::UNKNOWN_WIFI_TYPE);
        UNIT_ASSERT_EQUAL(WifiUtils::parseType("[WEP-EAP-CCMP][ESS]"), proto::WifiType::WEP);
        UNIT_ASSERT_EQUAL(WifiUtils::parseType("[WPA-EAP-CCMP][ESS]"), proto::WifiType::WPA);
        UNIT_ASSERT_EQUAL(WifiUtils::parseType("[ESS]"), proto::WifiType::OPEN);
    }

    Y_UNIT_TEST(testWifiDoesntCrashAtStartFromUncorrectWifi)
    {
        class TestWpaClient2: public TestWpaClient {
        public:
            std::string status(char* /* arg */) const override {
                std::lock_guard<std::mutex> g(lock_);
                buffer_.assign("wpa_state=COMPLETED\n");
                return buffer_;
            }
            bool isAttached() const override {
                return true;
            }
        };

        YandexIO::Configuration::TestGuard testGuard;
        auto deviceStateProvider = std::make_shared<mock::DeviceStateProvider>();
        auto userConfigProvider = std::make_shared<mock::UserConfigProvider>(nullptr);
        auto& config = getDeviceForTests() -> configuration() -> getMutableConfig(testGuard);
        auto& serviceConfig = config[WifiEndpoint::SERVICE_NAME];

        serviceConfig["accessPointStartTimeoutSec"] = "100";
        serviceConfig["waitForInternetAfterConnectSec"] = 1;

        TestWpaClient2 wpaClient;
        WifiManager wifiManager(getDeviceForTests(), wpaClient);
        WifiEndpoint wifiEndpoint(getDeviceForTests(), ipcFactoryForTests(), deviceStateProvider, userConfigProvider, wifiManager);
    }
}
