#include <yandex_io/libs/base/directives.h>
#include <yandex_io/libs/configuration/configuration.h>
#include <yandex_io/libs/iot/i_iot_discovery.h>
#include <yandex_io/libs/iot/i_iot_discovery_provider.h>
#include <yandex_io/libs/iot/null/null_iot_discovery_provider.h>
#include <yandex_io/libs/ipc/i_connector.h>
#include <yandex_io/libs/ipc/i_server.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/telemetry/null/null_metrica.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>
#include <yandex_io/tests/testlib/null_sdk/null_sdk_interface.h>

#include <smart_devices/libs/iot/capability/iot_capability.h>
#include <smart_devices/libs/iot/capability/interface/mocks/mock_i_iot_capability_events.h>

#include <fstream>
#include <future>

using namespace quasar;
using namespace testing;

struct Fixture: public QuasarUnitTestFixture {
    Fixture() {
        wifiSSID = "TEST_WIFI_SSID";
        wifiPassword = "TEST_WIFI_PWD";
        backendRegion = "TEST_REGION";
        backendToken = "TEST_TOKEN";
        backendSecret = "TEST_SECRET";
        backendCipher = "TEST_CIPHER";
        pairingToken = Fixture::backendRegion + Fixture::backendToken + Fixture::backendSecret;
    }

    void SetUp(NUnitTest::TTestContext& context) override {
        QuasarUnitTestFixture::SetUp(context);

        setDeviceForTests(std::make_shared<YandexIO::Device>(
            makeTestDeviceId(), makeTestConfiguration(), std::make_unique<NullMetrica>(), makeTestHAL()));

        auto& config = getDeviceForTests()->configuration()->getMutableConfig(testGuard);
        wifiStoragePath = config["firstrund"]["wifiStoragePath"].asString();
        {
            std::ofstream file(wifiStoragePath);
            if (!file.good()) {
                throw std::runtime_error("Cannot open file " + wifiStoragePath + " for writing");
            }

            quasar::proto::WifiConnect wifiInfo;
            wifiInfo.set_wifi_id(TString(wifiSSID));
            wifiInfo.set_password(TString(wifiPassword));
            file << wifiInfo.SerializeAsString();
        }
    }

    void TearDown(NUnitTest::TTestContext& context) override {
        ::unlink(wifiStoragePath.c_str());
        QuasarUnitTestFixture::TearDown(context);
    }

public:
    std::string wifiSSID;
    std::string wifiPassword;
    std::string backendRegion;
    std::string backendToken;
    std::string backendSecret;
    std::string backendCipher;
    std::string pairingToken;

    YandexIO::Configuration::TestGuard testGuard;
    std::string wifiStoragePath;
};

Y_UNIT_TEST_SUITE(IotCapabilityTest) {

    Y_UNIT_TEST_F(testDiscoveryHappyPath, Fixture) {
        auto sdk = std::make_shared<YandexIO::NullSDKInterface>();
        auto events = std::make_shared<YandexIO::MockIIotCapabilityEvents>();
        auto discoveryProvider = std::make_shared<NullIotDiscoveryProvider>();
        auto iotCapability = std::make_shared<YandexIO::IotCapability>(sdk, getDeviceForTests(), events, discoveryProvider);

        auto startedSync = std::make_unique<std::promise<void>>();
        EXPECT_CALL(*events, onIotCapabilityDiscoveryStarted()).WillOnce(Invoke([&]() { startedSync->set_value(); }));

        auto credentialsSync = std::make_unique<std::promise<void>>();
        EXPECT_CALL(*events, onIotCapabilityWifiCredentialsReceived()).WillOnce(Invoke([&]() { credentialsSync->set_value(); }));

        auto stoppedSync = std::make_unique<std::promise<void>>();
        EXPECT_CALL(*events, onIotCapabilityDiscoveryStopped()).WillOnce(Invoke([&]() { stoppedSync->set_value(); }));

        {
            Json::Value payload;
            payload["ssid"] = wifiSSID;
            payload["device_type"] = "TEST_DEVICE_TYPE";
            iotCapability->handleDirective(YandexIO::Directive::createClientAction(Directives::IOT_DISCOVERY_START, payload));
        }
        startedSync->get_future().get();

        {
            Json::Value payload;
            payload["ssid"] = wifiSSID;
            payload["password"] = wifiPassword;
            payload["token"] = backendToken;
            payload["cipher"] = backendCipher;
            iotCapability->handleDirective(YandexIO::Directive::createClientAction(Directives::IOT_DISCOVERY_CREDENTIALS, payload));
        }
        credentialsSync->get_future().get();

        {
            Json::Value payload;
            payload["result"] = "success";
            iotCapability->handleDirective(YandexIO::Directive::createClientAction(Directives::IOT_DISCOVERY_STOP, payload));
        }
        stoppedSync->get_future().get();
    }

    Y_UNIT_TEST_F(testDiscoveryStoppedOnTimeout, Fixture) {
        auto sdk = std::make_shared<YandexIO::NullSDKInterface>();
        auto events = std::make_shared<YandexIO::MockIIotCapabilityEvents>();
        auto discoveryProvider = std::make_shared<NullIotDiscoveryProvider>();
        auto iotCapability = std::make_shared<YandexIO::IotCapability>(sdk, getDeviceForTests(), events, discoveryProvider);

        auto startedSync = std::make_unique<std::promise<void>>();
        EXPECT_CALL(*events, onIotCapabilityDiscoveryStarted()).WillOnce(Invoke([&]() { startedSync->set_value(); }));

        auto stoppedSync = std::make_unique<std::promise<void>>();
        EXPECT_CALL(*events, onIotCapabilityDiscoveryStopped()).WillOnce(Invoke([&]() { stoppedSync->set_value(); }));

        {
            Json::Value payload;
            payload["ssid"] = wifiSSID;
            payload["device_type"] = "TEST_DEVICE_TYPE";
            payload["timeout_ms"] = 500;
            iotCapability->handleDirective(YandexIO::Directive::createClientAction(Directives::IOT_DISCOVERY_START, payload));
        }
        startedSync->get_future().get();
        stoppedSync->get_future().get();
    }
}
