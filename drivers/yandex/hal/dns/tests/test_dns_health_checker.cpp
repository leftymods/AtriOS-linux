#include <smart_devices/libs/dns/dns_health_checker.h>

#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>
#include <yandex_io/tests/testlib/null_sdk/null_sdk_interface.h>
#include <yandex_io/tests/testlib/test_callback_queue.h>

#include <library/cpp/testing/gmock_in_unittest/gmock.h>
#include <library/cpp/testing/unittest/registar.h>

using testing::_;
using namespace quasar;

using HStatus = dns::RuntimeDnsConfig::IDnsHealthChecker::Status;
using ConfigStatus = YandexIO::BackendConfigObserver::BackendConfigHandleStatus;

namespace {

    YandexIO::SDKState makeWifiState(bool hasInternet) {
        YandexIO::SDKState state;
        state.wifiState.isWifiConnected = true;
        state.wifiState.isInternetReachable = hasInternet;
        return state;
    }

} // namespace

Y_UNIT_TEST_SUITE_F(TestDnsHealthChecker, QuasarUnitTestFixture) {
    Y_UNIT_TEST(simple) {
        const auto worker = std::make_shared<TestCallbackQueue>();
        const auto healthChecker = dns::DnsHealthChecker::create(worker, std::chrono::seconds(30), std::chrono::seconds(0));
        healthChecker->start();

        // not enough data by default
        UNIT_ASSERT_EQUAL(HStatus::POSTPONE, healthChecker->healthCheck());

        // generate_204 fail
        healthChecker->onSDKState(makeWifiState(false));
        UNIT_ASSERT_EQUAL(HStatus::FAILED, healthChecker->healthCheck());
        // generate_204 ok. Postpone, since we wait for get_sync_info
        healthChecker->onSDKState(makeWifiState(true));
        UNIT_ASSERT_EQUAL(HStatus::POSTPONE, healthChecker->healthCheck());

        // Ok by GetSyncInfo success
        healthChecker->onBackendConfigHandleStatus(ConfigStatus::OK);
        UNIT_ASSERT_EQUAL(HStatus::OK, healthChecker->healthCheck());

        // Got GetSyncInfo status, but generate_204 check failed -> fallback
        healthChecker->onSDKState(makeWifiState(false));
        UNIT_ASSERT_EQUAL(HStatus::FAILED, healthChecker->healthCheck());
    }

    Y_UNIT_TEST(failByTimeout) {
        const auto worker = std::make_shared<TestCallbackQueue>();
        const auto healthChecker = dns::DnsHealthChecker::create(worker, std::chrono::milliseconds(30), std::chrono::seconds(0));
        healthChecker->start();

        // get sync info success timeout is 30ms. Check should fail even with good pinger status
        healthChecker->onSDKState(makeWifiState(true));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        UNIT_ASSERT_EQUAL(HStatus::FAILED, healthChecker->healthCheck());
    }

    Y_UNIT_TEST(getSyncInfoSkipTimeout) {
        const auto worker = std::make_shared<TestCallbackQueue>();
        const auto healthChecker = dns::DnsHealthChecker::create(worker, std::chrono::milliseconds(30), std::chrono::milliseconds(100));
        healthChecker->start();
        healthChecker->onSDKState(makeWifiState(true));
        healthChecker->onBackendConfigHandleStatus(ConfigStatus::OK);
        // this check should return POSTPONE because onBackendConfigHandleStatus was called before
        // firstGetSyncInfoTimeout passed
        UNIT_ASSERT_EQUAL(HStatus::POSTPONE, healthChecker->healthCheck());

        // Next check should work, since 100ms timeout will pass
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        healthChecker->onBackendConfigHandleStatus(ConfigStatus::OK);
        UNIT_ASSERT_EQUAL(HStatus::OK, healthChecker->healthCheck());
    }

    Y_UNIT_TEST(runtimeTimeout) {
        const auto worker = std::make_shared<TestCallbackQueue>();
        const auto healthChecker = dns::DnsHealthChecker::create(worker, std::chrono::seconds(100), std::chrono::seconds(0));
        healthChecker->start();
        healthChecker->onSDKState(makeWifiState(true));

        // set zero timeout, so check fail
        healthChecker->setSuccessTimeout(std::chrono::seconds(0));
        UNIT_ASSERT_EQUAL(HStatus::FAILED, healthChecker->healthCheck());

        // fallback to defaults. Timeout didn't pass yet, so should be postpone check
        healthChecker->setSuccessTimeout(std::nullopt);
        UNIT_ASSERT_EQUAL(HStatus::POSTPONE, healthChecker->healthCheck());
    }

} // suite
