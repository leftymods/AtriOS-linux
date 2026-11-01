#include <smart_devices/libs/dns/runtime_dns_config.h>

#include <yandex_io/libs/telemetry/null/null_metrica.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/base/persistent_file.h>

#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>
#include <yandex_io/tests/testlib/null_sdk/null_sdk_interface.h>
#include <yandex_io/tests/testlib/test_callback_queue.h>

#include <library/cpp/testing/gmock_in_unittest/gmock.h>
#include <library/cpp/testing/unittest/registar.h>

#include <util/folder/path.h>

using testing::_;
using namespace quasar;

using HStatus = dns::RuntimeDnsConfig::IDnsHealthChecker::Status;

namespace {

    class DnsUpdaterMock: public dns::RuntimeDnsConfig::IDnsUpdater {
    public:
        MOCK_METHOD(void, setCustomServers, (const std::vector<TIpv6Address>&), (override));
        MOCK_METHOD(void, removeCustomServers, (), (override));
    };

    class DnsHealthCheckerMock: public dns::RuntimeDnsConfig::IDnsHealthChecker {
    public:
        MOCK_METHOD(void, start, (), (override));
        MOCK_METHOD(void, reset, (), (override));
        MOCK_METHOD(HStatus, healthCheck, (), (const, override));
        MOCK_METHOD(void, setSuccessTimeout, (std::optional<std::chrono::milliseconds>), (override));
    };

    class SDKMock: public YandexIO::NullSDKInterface {
    public:
        void addBackendConfigObserver(std::weak_ptr<YandexIO::BackendConfigObserver> observer) override {
            auto configObserver = observer.lock();
            Y_VERIFY(configObserver);
            configObserver_ = std::move(configObserver);
        }

        void notifyConfig(const std::string& name, const std::string& config) {
            Y_VERIFY(configObserver_);
            configObserver_->onSystemConfig(name, config);
        }

    private:
        std::shared_ptr<YandexIO::BackendConfigObserver> configObserver_;
    };

    MATCHER_P(VerifyDnsServerIps, targetIps, "Description") {
        const std::vector<TIpv6Address>& ips = arg;
        if (targetIps.size() != ips.size()) {
            *result_listener << "TargetIps vector size != method input ips vector size\n";
            return false;
        }
        for (size_t i = 0; i < ips.size(); ++i) {
            const auto inIp = ips[i].ToString();
            const auto targetIp = targetIps[i];
            if (inIp != targetIp) {
                *result_listener << "Input ip != targetIp: [" << inIp << " != " << targetIp << "]\n";
                return false;
            }
        }
        return true;
    }

    class RuntimeDnsFixture: public QuasarUnitTestFixture {
    public:
        RuntimeDnsFixture()
            : QuasarUnitTestFixture()
            , dnsUpdaterMock_(std::make_shared<DnsUpdaterMock>())
            , worker_(std::make_shared<TestCallbackQueue>())
            , healthCheckerMock_(std::make_shared<DnsHealthCheckerMock>())
            , telemetry_(std::make_shared<NullMetrica>())
            , backupFile_(JoinFsPaths(tryGetRamDrivePath(), "backup.json"))
        {
        }

        ~RuntimeDnsFixture() {
            backupFile_.ForceDelete();
        }

    protected:
        const std::shared_ptr<DnsUpdaterMock> dnsUpdaterMock_;
        const std::shared_ptr<TestCallbackQueue> worker_;
        const std::shared_ptr<DnsHealthCheckerMock> healthCheckerMock_;
        const std::shared_ptr<YandexIO::ITelemetry> telemetry_;
        const TFsPath backupFile_;
    };

} // namespace

Y_UNIT_TEST_SUITE_F(TestRuntimeDnsConfig, RuntimeDnsFixture) {
    Y_UNIT_TEST(SetupRemoveByConfig) {
        SDKMock sdk;
        dns::RuntimeDnsConfig dnsConfig(dnsUpdaterMock_, healthCheckerMock_, worker_, telemetry_, std::chrono::minutes(5), backupFile_.GetPath());
        dnsConfig.subscribeToSDK(sdk);

        {
            testing::InSequence seq;
            EXPECT_CALL(*dnsUpdaterMock_, setCustomServers(_));
            EXPECT_CALL(*healthCheckerMock_, start());
            EXPECT_CALL(*dnsUpdaterMock_, removeCustomServers());
            EXPECT_CALL(*healthCheckerMock_, reset());
        }

        sdk.notifyConfig("dns", "{\"customDnsEnabled\":true, \"servers\": [\"8.8.8.8\"]}");
        sdk.notifyConfig("dns", "{}");
    }

    Y_UNIT_TEST(DisableByEnabledFalse) {
        SDKMock sdk;

        dns::RuntimeDnsConfig dnsConfig(dnsUpdaterMock_, healthCheckerMock_, worker_, telemetry_, std::chrono::minutes(5), backupFile_.GetPath());
        dnsConfig.subscribeToSDK(sdk);

        {
            testing::InSequence seq;
            EXPECT_CALL(*dnsUpdaterMock_, setCustomServers(_));
            EXPECT_CALL(*healthCheckerMock_, start());
            EXPECT_CALL(*dnsUpdaterMock_, removeCustomServers());
            EXPECT_CALL(*healthCheckerMock_, reset());
        }

        sdk.notifyConfig("dns", "{\"customDnsEnabled\":true, \"servers\": [\"8.8.8.8\"]}");
        sdk.notifyConfig("dns", "{\"customDnsEnabled\":false, \"servers\": [\"8.8.8.8\"]}");
    }

    Y_UNIT_TEST(DisableByEmptyServersList) {
        SDKMock sdk;

        dns::RuntimeDnsConfig dnsConfig(dnsUpdaterMock_, healthCheckerMock_, worker_, telemetry_, std::chrono::minutes(5), backupFile_.GetPath());
        dnsConfig.subscribeToSDK(sdk);

        {
            testing::InSequence seq;
            EXPECT_CALL(*dnsUpdaterMock_, setCustomServers(_));
            EXPECT_CALL(*healthCheckerMock_, start());
            EXPECT_CALL(*dnsUpdaterMock_, removeCustomServers());
            EXPECT_CALL(*healthCheckerMock_, reset());
        }

        sdk.notifyConfig("dns", "{\"customDnsEnabled\":true, \"servers\": [\"8.8.8.8\"]}");
        sdk.notifyConfig("dns", "{\"customDnsEnabled\":true, \"servers\": []}");
    }

    Y_UNIT_TEST(SetCustomServersValues) {
        SDKMock sdk;

        dns::RuntimeDnsConfig dnsConfig(dnsUpdaterMock_, healthCheckerMock_, worker_, telemetry_, std::chrono::minutes(5), backupFile_.GetPath());
        dnsConfig.subscribeToSDK(sdk);

        {
            testing::InSequence seq;
            EXPECT_CALL(*dnsUpdaterMock_, setCustomServers(VerifyDnsServerIps(std::vector<std::string>({"8.8.8.8"}))));
            EXPECT_CALL(*dnsUpdaterMock_, setCustomServers(VerifyDnsServerIps(std::vector<std::string>({"8.8.8.8", "1.1.1.1"}))));
            EXPECT_CALL(*dnsUpdaterMock_, setCustomServers(VerifyDnsServerIps(std::vector<std::string>({"8.8.8.8", "1.1.1.1"}))));
            EXPECT_CALL(*dnsUpdaterMock_, removeCustomServers());
            EXPECT_CALL(*dnsUpdaterMock_, setCustomServers(VerifyDnsServerIps(std::vector<std::string>({"2.2.2.2"}))));
        }

        sdk.notifyConfig("dns", "{\"customDnsEnabled\":true, \"servers\": [\"8.8.8.8\"]}");
        sdk.notifyConfig("dns", "{\"customDnsEnabled\":true, \"servers\": [\"8.8.8.8\", \"1.1.1.1\"]}");
        // ensure that not valid ips are skipped
        sdk.notifyConfig("dns", "{\"customDnsEnabled\":true, \"servers\": [\"noAnIp\", \"8.8.8.8\", \"1.1.1.1\"]}");
        sdk.notifyConfig("dns", "{\"customDnsEnabled\":true, \"servers\": [\"8.8.8.8.1\"]}");
        sdk.notifyConfig("dns", "{\"customDnsEnabled\":true, \"servers\": [\"128.0.0.256\", \"2.2.2.2\"]}");
    }

    Y_UNIT_TEST(healthCheckFailed) {
        SDKMock sdk;

        dns::RuntimeDnsConfig dnsConfig(dnsUpdaterMock_, healthCheckerMock_, worker_, telemetry_, std::chrono::minutes(5), backupFile_.GetPath());
        dnsConfig.subscribeToSDK(sdk);

        {
            testing::InSequence seq;
            EXPECT_CALL(*dnsUpdaterMock_, setCustomServers(_));
            EXPECT_CALL(*healthCheckerMock_, start());
            EXPECT_CALL(*healthCheckerMock_, healthCheck()).WillOnce(testing::Return(HStatus::FAILED));
            EXPECT_CALL(*dnsUpdaterMock_, removeCustomServers());
            EXPECT_CALL(*healthCheckerMock_, reset());
        }
        sdk.notifyConfig("dns", "{\"customDnsEnabled\":true, \"servers\": [\"8.8.8.8\"]}");

        worker_->pumpDelayedCallback(); // execute delayed health check. Should fallback custom server
    }

    Y_UNIT_TEST(healthCheckPostpone) {
        SDKMock sdk;

        dns::RuntimeDnsConfig dnsConfig(dnsUpdaterMock_, healthCheckerMock_, worker_, telemetry_, std::chrono::minutes(5), backupFile_.GetPath());
        dnsConfig.subscribeToSDK(sdk);
        {
            testing::InSequence seq;
            EXPECT_CALL(*dnsUpdaterMock_, setCustomServers(_));
            EXPECT_CALL(*healthCheckerMock_, start());
            EXPECT_CALL(*healthCheckerMock_, healthCheck()).Times(2).WillRepeatedly(testing::Return(HStatus::POSTPONE));
            EXPECT_CALL(*healthCheckerMock_, healthCheck()).WillOnce(testing::Return(HStatus::OK));
            EXPECT_CALL(*healthCheckerMock_, reset());
        }

        sdk.notifyConfig("dns", "{\"customDnsEnabled\":true, \"servers\": [\"8.8.8.8\"]}");

        // postpone 2 times. And return OK status once, so health checks will be stopped
        UNIT_ASSERT_VALUES_EQUAL(worker_->delayedCallbackCount(), 1);
        worker_->pumpDelayedCallback();
        UNIT_ASSERT_VALUES_EQUAL(worker_->delayedCallbackCount(), 1);
        worker_->pumpDelayedCallback();
        UNIT_ASSERT_VALUES_EQUAL(worker_->delayedCallbackCount(), 1);
        worker_->pumpDelayedCallback();

        UNIT_ASSERT_VALUES_EQUAL(worker_->delayedCallbackCount(), 0);

    }

    Y_UNIT_TEST(healthCheckFailedAfterPostpone) {
        SDKMock sdk;

        dns::RuntimeDnsConfig dnsConfig(dnsUpdaterMock_, healthCheckerMock_, worker_, telemetry_, std::chrono::minutes(5), backupFile_.GetPath());
        dnsConfig.subscribeToSDK(sdk);
        {
            testing::InSequence seq;
            EXPECT_CALL(*dnsUpdaterMock_, setCustomServers(_));
            EXPECT_CALL(*healthCheckerMock_, start());
            EXPECT_CALL(*healthCheckerMock_, healthCheck()).Times(2).WillRepeatedly(testing::Return(HStatus::POSTPONE));
            EXPECT_CALL(*healthCheckerMock_, healthCheck()).WillOnce(testing::Return(HStatus::FAILED));
            EXPECT_CALL(*dnsUpdaterMock_, removeCustomServers());
            EXPECT_CALL(*healthCheckerMock_, reset());
        }

        sdk.notifyConfig("dns", "{\"customDnsEnabled\":true, \"servers\": [\"8.8.8.8\"]}");

        // postpone 2 times. And return FAILED. settings should be restored and health checks should stop
        UNIT_ASSERT_VALUES_EQUAL(worker_->delayedCallbackCount(), 1);
        worker_->pumpDelayedCallback();
        UNIT_ASSERT_VALUES_EQUAL(worker_->delayedCallbackCount(), 1);
        worker_->pumpDelayedCallback();
        UNIT_ASSERT_VALUES_EQUAL(worker_->delayedCallbackCount(), 1);
        worker_->pumpDelayedCallback();

        UNIT_ASSERT_VALUES_EQUAL(worker_->delayedCallbackCount(), 0);
    }

    Y_UNIT_TEST(saveSettingsToBackupFile) {
        SDKMock sdk;

        dns::RuntimeDnsConfig dnsConfig(dnsUpdaterMock_, healthCheckerMock_, worker_, telemetry_, std::chrono::minutes(5), backupFile_.GetPath());
        dnsConfig.subscribeToSDK(sdk);
        Json::Value runtimeConfig;
        runtimeConfig["customDnsEnabled"] = true;
        runtimeConfig["servers"] = Json::arrayValue;
        runtimeConfig["servers"].append(Json::Value("8.8.8.8"));
        // install config
        {
            sdk.notifyConfig("dns", jsonToString(runtimeConfig));
            UNIT_ASSERT(backupFile_.Exists());
            const auto configOpt = tryReadJsonFromFile(backupFile_.GetPath());
            UNIT_ASSERT(configOpt.has_value());
            const auto& json = *configOpt;
            UNIT_ASSERT(json.isMember("runtimeConfig"));
            UNIT_ASSERT_EQUAL(json["runtimeConfig"], runtimeConfig);
            // health check didn't start yet
            UNIT_ASSERT(!json.isMember("healthCheckPassed"));
        }

        // check file after healthcheck (ok status)
        {
            EXPECT_CALL(*healthCheckerMock_, healthCheck()).WillOnce(testing::Return(HStatus::OK));
            worker_->pumpDelayedCallback();
            UNIT_ASSERT(backupFile_.Exists());
            const auto configOpt = tryReadJsonFromFile(backupFile_.GetPath());
            UNIT_ASSERT(configOpt.has_value());
            const auto& json = *configOpt;
            UNIT_ASSERT(json.isMember("runtimeConfig"));
            UNIT_ASSERT_EQUAL(json["runtimeConfig"], runtimeConfig);
            // health check passed
            UNIT_ASSERT(json.isMember("healthCheckPassed"));
            UNIT_ASSERT(json["healthCheckPassed"].asBool());
        }

        // update config with 1 more server
        runtimeConfig["servers"].append(Json::Value("0.0.0.0"));
        {
            sdk.notifyConfig("dns", jsonToString(runtimeConfig));
            UNIT_ASSERT(backupFile_.Exists());
            const auto configOpt = tryReadJsonFromFile(backupFile_.GetPath());
            UNIT_ASSERT(configOpt.has_value());
            const auto& json = *configOpt;
            UNIT_ASSERT(json.isMember("runtimeConfig"));
            UNIT_ASSERT_EQUAL(json["runtimeConfig"], runtimeConfig);
            // health check for new config didn't start yet
            UNIT_ASSERT(!json.isMember("healthCheckPassed"));
        }

        // check file after healthcheck (failed status)
        {
            EXPECT_CALL(*healthCheckerMock_, healthCheck()).WillOnce(testing::Return(HStatus::FAILED));
            worker_->pumpDelayedCallback();
            UNIT_ASSERT(backupFile_.Exists());
            const auto configOpt = tryReadJsonFromFile(backupFile_.GetPath());
            UNIT_ASSERT(configOpt.has_value());
            const auto& json = *configOpt;
            UNIT_ASSERT(json.isMember("runtimeConfig"));
            UNIT_ASSERT_EQUAL(json["runtimeConfig"], runtimeConfig);
            // health check failed
            UNIT_ASSERT(json.isMember("healthCheckPassed"));
            UNIT_ASSERT(!json["healthCheckPassed"].asBool());
        }
    }

    Y_UNIT_TEST(recoverFromFile) {
        SDKMock sdk;

        const auto saveJsonToFile = [](const auto& json, const auto& filename) {
            TransactionFile file(filename);
            file.write(jsonToString(json));
            file.commit();
        };

        // Custom dns is disabled in config. Should call removeCustomServers on start
        {
            Json::Value json;
            json["runtimeConfig"]["customDnsEnabled"] = false;
            saveJsonToFile(json, backupFile_.GetPath());
            testing::InSequence seq;
            EXPECT_CALL(*dnsUpdaterMock_, removeCustomServers());

            dns::RuntimeDnsConfig dnsConfig(dnsUpdaterMock_, healthCheckerMock_, worker_, telemetry_, std::chrono::minutes(5), backupFile_.GetPath());

            backupFile_.ForceDelete();
        }

        // Custom dns is enabled, but there is no health check status. Should setup config and
        // start healthcheck
        {
            Json::Value json;
            json["runtimeConfig"]["customDnsEnabled"] = true;
            json["runtimeConfig"]["servers"] = Json::arrayValue;
            json["runtimeConfig"]["servers"].append(Json::Value("8.8.8.8"));
            saveJsonToFile(json, backupFile_.GetPath());
            testing::InSequence seq;
            EXPECT_CALL(*dnsUpdaterMock_, setCustomServers(VerifyDnsServerIps(std::vector<std::string>({"8.8.8.8"}))));
            EXPECT_CALL(*healthCheckerMock_, start());

            dns::RuntimeDnsConfig dnsConfig(dnsUpdaterMock_, healthCheckerMock_, worker_, telemetry_, std::chrono::minutes(5), backupFile_.GetPath());
            dnsConfig.subscribeToSDK(sdk);
            sdk.notifyConfig("dns", jsonToString(json["runtimeConfig"]));

            backupFile_.ForceDelete();
        }

        // Custom dns is enabled. Health Check passed. None of methods should be called
        // State should be kept untoched (last dns settings should be saved by system)
        {
            Json::Value json;
            json["runtimeConfig"]["customDnsEnabled"] = true;
            json["runtimeConfig"]["servers"] = Json::arrayValue;
            json["runtimeConfig"]["servers"].append(Json::Value("8.8.8.8"));
            json["healthCheckPassed"] = true;
            saveJsonToFile(json, backupFile_.GetPath());
            testing::InSequence seq;
            EXPECT_CALL(*dnsUpdaterMock_, setCustomServers(_)).Times(0);
            EXPECT_CALL(*dnsUpdaterMock_, removeCustomServers()).Times(0);
            EXPECT_CALL(*healthCheckerMock_, start()).Times(0);

            dns::RuntimeDnsConfig dnsConfig(dnsUpdaterMock_, healthCheckerMock_, worker_, telemetry_, std::chrono::minutes(5), backupFile_.GetPath());
            dnsConfig.subscribeToSDK(sdk);
            sdk.notifyConfig("dns", jsonToString(json["runtimeConfig"]));

            backupFile_.ForceDelete();
        }

        // Custom dns is enabled. Health Check failed. None of methods should be called
        // State should be kept untoched (last dns settings should be saved by system)
        {
            Json::Value json;
            json["runtimeConfig"]["customDnsEnabled"] = true;
            json["runtimeConfig"]["servers"] = Json::arrayValue;
            json["runtimeConfig"]["servers"].append(Json::Value("8.8.8.8"));
            json["healthCheckPassed"] = false;
            saveJsonToFile(json, backupFile_.GetPath());
            testing::InSequence seq;
            EXPECT_CALL(*dnsUpdaterMock_, setCustomServers(_)).Times(0);
            EXPECT_CALL(*dnsUpdaterMock_, removeCustomServers()).Times(0);
            EXPECT_CALL(*healthCheckerMock_, start()).Times(0);

            dns::RuntimeDnsConfig dnsConfig(dnsUpdaterMock_, healthCheckerMock_, worker_, telemetry_, std::chrono::minutes(5), backupFile_.GetPath());
            dnsConfig.subscribeToSDK(sdk);
            sdk.notifyConfig("dns", jsonToString(json["runtimeConfig"]));

            backupFile_.ForceDelete();
        }
    }

    Y_UNIT_TEST(TestSetTimeouts) {
        SDKMock sdk;
        dns::RuntimeDnsConfig dnsConfig(dnsUpdaterMock_, healthCheckerMock_, worker_, telemetry_, std::chrono::minutes(5), backupFile_.GetPath());
        dnsConfig.subscribeToSDK(sdk);

        {
            testing::InSequence seq;
            std::optional<std::chrono::milliseconds> timeout = std::chrono::seconds(300);
            EXPECT_CALL(*healthCheckerMock_, setSuccessTimeout(timeout));
            timeout.reset();
            EXPECT_CALL(*healthCheckerMock_, setSuccessTimeout(timeout));
        }

        sdk.notifyConfig("dns", "{\"customDnsEnabled\":true, \"healthCheckPeriodSec\" : 12, \"healthCheckTimeoutSec\": 300, \"servers\": [\"8.8.8.8\"]}");
        UNIT_ASSERT_EQUAL(worker_->firstDelayedCallbackTimeout(), std::chrono::seconds(12));
        worker_->pumpDelayedCallback(); // drop callback
        sdk.notifyConfig("dns", "{\"customDnsEnabled\":true, \"servers\": [\"8.8.8.8\"]}");
        UNIT_ASSERT_EQUAL(worker_->firstDelayedCallbackTimeout(), std::chrono::minutes(5));
    }

    Y_UNIT_TEST(TestSameConfig) {
        SDKMock sdk;
        dns::RuntimeDnsConfig dnsConfig(dnsUpdaterMock_, healthCheckerMock_, worker_, telemetry_, std::chrono::minutes(5), backupFile_.GetPath());
        dnsConfig.subscribeToSDK(sdk);

        // ensure that providing same config do not cause removeCustomServers/setCustomServers calls
        {
            testing::InSequence seq;
            EXPECT_CALL(*dnsUpdaterMock_, removeCustomServers()).Times(1);
        }

        sdk.notifyConfig("dns", "{}");
        sdk.notifyConfig("dns", "{}");
        sdk.notifyConfig("dns", "{}");

        {
            testing::InSequence seq;
            EXPECT_CALL(*dnsUpdaterMock_, setCustomServers(VerifyDnsServerIps(std::vector<std::string>({"8.8.8.8"})))).Times(1);
        }

        Json::Value json;
        json["runtimeConfig"]["customDnsEnabled"] = true;
        json["runtimeConfig"]["servers"] = Json::arrayValue;
        json["runtimeConfig"]["servers"].append(Json::Value("8.8.8.8"));
        sdk.notifyConfig("dns", jsonToString(json["runtimeConfig"]));
        sdk.notifyConfig("dns", jsonToString(json["runtimeConfig"]));
        sdk.notifyConfig("dns", jsonToString(json["runtimeConfig"]));
    }

} // TestRuntimeDnsConfig suite
