#include <yandex_io/modules/audio_input/vqe/feedback_shift_metrics/feedback_shift_logger.h>

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/telemetry/null/null_metrica.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <library/cpp/testing/gmock_in_unittest/gmock.h>
#include <library/cpp/testing/unittest/registar.h>

#include <future>
#include <thread>

using namespace YandexIO;
using namespace quasar;
using testing::_;

namespace {

    constexpr auto ZERO = std::chrono::seconds(0);
    constexpr auto HOUR = std::chrono::hours(1);

    class VqeEngineMock: public VQEEngine {
    public:
        MOCK_METHOD(void, process, (const std::vector<float>& inputMic, const std::vector<float>& inputSpk, double& doaAngle, bool& speechDetected), (override));

        MOCK_METHOD(ChannelCount, getInputChannelCount, (), (const, override));
        MOCK_METHOD(size_t, getOutputChannelCount, (ChannelData::Type), (const, override));
        MOCK_METHOD(std::span<const float>, getOutputChannelData, (ChannelData::Type, size_t), (const, override));

        MOCK_METHOD(std::optional<int>, getFeedbackShift, (), (const, override));
        MOCK_METHOD(std::optional<float>, getFeedbackShiftCorrelation, (), (const, override));
    };

    class TelemetryMock: public NullMetrica {
    public:
        MOCK_METHOD(void, reportEvent, (const std::string&, const std::string&, ITelemetry::Flags), (override));
    };

    bool floatEqual(float a, float b) {
        return std::fabs(a - b) < 0.1; // test works with integrals
    }

    bool verifyJson(const std::string& jsonString, std::optional<int> shift, std::optional<float> correlation, const std::string& vqeType) {
        const Json::Value val = parseJson(jsonString);
        if (!shift.has_value()) {
            if (!val["feedback_shift"].isNull()) {
                return false;
            }
        } else if (val["feedback_shift"].asInt() != *shift) {
            return false;
        }

        if (!correlation.has_value()) {
            if (!val["correlation"].isNull()) {
                return false;
            }
        } else if (!floatEqual(val["correlation"].asFloat(), *correlation)) {
            return false;
        }

        if (val["vqeType"].asString() != vqeType) {
            return false;
        }
        return true;
    }

    MATCHER_P3(VerifyMetric, shift, correlation, vqeType, "description") {
        if (!verifyJson(arg, shift, correlation, vqeType)) {
            *result_listener << "Invalid json: " << arg;
            *result_listener << "\nExpect shift: " << (shift ? std::to_string(*shift) : "NONE");
            *result_listener << "\nExpect correlation: " << (correlation ? std::to_string(*correlation) : "NONE");
            *result_listener << "\nExpect vqeType: " << vqeType;
            return false;
        }
        return true;
    }

} // namespace

Y_UNIT_TEST_SUITE(TestFeedbackShiftLogger) {

    Y_UNIT_TEST_F(testSendMetricaWhenCalculated, QuasarUnitTestFixture) {
        auto telemetry = std::make_shared<TelemetryMock>();
        FeedbackShiftLogger logger(telemetry, HOUR, ZERO);
        VqeEngineMock engine;
        std::optional<int> shift = 1;
        std::optional<float> correlation = 1;
        EXPECT_CALL(engine, getFeedbackShift()).WillOnce(testing::Return(shift));
        EXPECT_CALL(engine, getFeedbackShiftCorrelation()).WillOnce(testing::Return(correlation));
        EXPECT_CALL(*telemetry, reportEvent(_, VerifyMetric(shift, correlation, "stub"), _));
        logger.log(engine, "stub");
    }

    Y_UNIT_TEST_F(testSendMetricaWhenShiftChanged, QuasarUnitTestFixture) {
        auto telemetry = std::make_shared<TelemetryMock>();
        FeedbackShiftLogger logger(telemetry, HOUR, ZERO);
        VqeEngineMock engine;
        // init shift
        std::optional<int> shift = 1;
        const std::optional<float> correlation = 1;
        EXPECT_CALL(engine, getFeedbackShift()).WillOnce(testing::Return(shift));
        EXPECT_CALL(engine, getFeedbackShiftCorrelation()).WillOnce(testing::Return(correlation));
        EXPECT_CALL(*telemetry, reportEvent(_, VerifyMetric(shift, correlation, "stub"), _));
        logger.log(engine, "stub");
        // bump shift
        shift = 2;
        EXPECT_CALL(engine, getFeedbackShift()).WillOnce(testing::Return(shift));
        EXPECT_CALL(engine, getFeedbackShiftCorrelation()).WillOnce(testing::Return(correlation));
        EXPECT_CALL(*telemetry, reportEvent(_, VerifyMetric(shift, correlation, "stub"), _));
        logger.log(engine, "stub");
        // fallback to null
        shift = std::nullopt;
        EXPECT_CALL(engine, getFeedbackShift()).WillOnce(testing::Return(shift));
        EXPECT_CALL(engine, getFeedbackShiftCorrelation()).WillOnce(testing::Return(correlation));
        EXPECT_CALL(*telemetry, reportEvent(_, VerifyMetric(shift, correlation, "stub"), _));
        logger.log(engine, "stub");
    }

    Y_UNIT_TEST_F(testSendMetricaWhenCorrelation, QuasarUnitTestFixture) {
        auto telemetry = std::make_shared<TelemetryMock>();
        FeedbackShiftLogger logger(telemetry, HOUR, ZERO);
        VqeEngineMock engine;
        // init shift
        const std::optional<int> shift = 1;
        std::optional<float> correlation = 1;
        EXPECT_CALL(engine, getFeedbackShift()).WillOnce(testing::Return(shift));
        EXPECT_CALL(engine, getFeedbackShiftCorrelation()).WillOnce(testing::Return(correlation));
        EXPECT_CALL(*telemetry, reportEvent(_, VerifyMetric(shift, correlation, "stub"), _));
        logger.log(engine, "stub");
        // bump correlation
        correlation = 2;
        EXPECT_CALL(engine, getFeedbackShift()).WillOnce(testing::Return(shift));
        EXPECT_CALL(engine, getFeedbackShiftCorrelation()).WillOnce(testing::Return(correlation));
        EXPECT_CALL(*telemetry, reportEvent(_, VerifyMetric(shift, correlation, "stub"), _));
        logger.log(engine, "stub");
        // fallback to null
        correlation = std::nullopt;
        EXPECT_CALL(engine, getFeedbackShift()).WillOnce(testing::Return(shift));
        EXPECT_CALL(engine, getFeedbackShiftCorrelation()).WillOnce(testing::Return(correlation));
        EXPECT_CALL(*telemetry, reportEvent(_, VerifyMetric(shift, correlation, "stub"), _));
        logger.log(engine, "stub");
    }

    Y_UNIT_TEST_F(testVqeTypeChanged, QuasarUnitTestFixture) {
        auto telemetry = std::make_shared<TelemetryMock>();
        FeedbackShiftLogger logger(telemetry, HOUR, ZERO);
        VqeEngineMock engine;
        // init shift
        std::optional<int> shift = 1;
        std::optional<float> correlation = 1;
        EXPECT_CALL(engine, getFeedbackShift()).WillOnce(testing::Return(shift));
        EXPECT_CALL(engine, getFeedbackShiftCorrelation()).WillOnce(testing::Return(correlation));
        EXPECT_CALL(*telemetry, reportEvent(_, VerifyMetric(shift, correlation, "stub"), _));
        logger.log(engine, "stub");
        // change vqe name
        EXPECT_CALL(engine, getFeedbackShift()).WillOnce(testing::Return(shift));
        EXPECT_CALL(engine, getFeedbackShiftCorrelation()).WillOnce(testing::Return(correlation));
        EXPECT_CALL(*telemetry, reportEvent(_, VerifyMetric(shift, correlation, "stub2"), _));
        logger.log(engine, "stub2");
        // change vqe name again
        EXPECT_CALL(engine, getFeedbackShift()).WillOnce(testing::Return(shift));
        EXPECT_CALL(engine, getFeedbackShiftCorrelation()).WillOnce(testing::Return(correlation));
        EXPECT_CALL(*telemetry, reportEvent(_, VerifyMetric(shift, correlation, "stub3"), _));
        logger.log(engine, "stub3");
    }

    Y_UNIT_TEST_F(testTrottle, QuasarUnitTestFixture) {
        auto telemetry = std::make_shared<TelemetryMock>();
        // set up big trottle time diff, to make sure that vqe change always trigger metrics
        const auto pingPeriod = std::chrono::hours(2);
        const auto trottlePeriod = std::chrono::hours(1);
        FeedbackShiftLogger logger(telemetry, pingPeriod, trottlePeriod);
        VqeEngineMock engine;
        // init shift
        const std::optional<int> shift = 0;
        const std::optional<float> correlation = 0;

        EXPECT_CALL(engine, getFeedbackShift()).WillOnce(testing::Return(shift));
        EXPECT_CALL(engine, getFeedbackShiftCorrelation()).WillOnce(testing::Return(correlation));
        EXPECT_CALL(*telemetry, reportEvent(_, VerifyMetric(shift, correlation, "stub"), _));
        logger.log(engine, "stub");
        // Call should be satisfied only once
        logger.log(engine, "stub");
        logger.log(engine, "stub");
    }

    Y_UNIT_TEST_F(testPing, QuasarUnitTestFixture) {
        auto telemetry = std::make_shared<TelemetryMock>();
        // set up big trottle time diff, to make sure that vqe change always trigger metrics
        const auto pingPeriod = std::chrono::seconds(1);
        FeedbackShiftLogger logger(telemetry, pingPeriod, ZERO);
        VqeEngineMock engine;
        // init shift
        const std::optional<int> shift = 0;
        const std::optional<float> correlation = 0;
        std::promise<void> ping1;
        std::promise<void> ping2;

        EXPECT_CALL(engine, getFeedbackShift()).WillRepeatedly(testing::Return(shift));
        EXPECT_CALL(engine, getFeedbackShiftCorrelation()).WillRepeatedly(testing::Return(correlation));
        EXPECT_CALL(*telemetry, reportEvent(_, VerifyMetric(shift, correlation, "stub"), _))
            .Times(testing::AtLeast(3))
            .WillOnce(testing::Return()) // do nothing on first send
            .WillOnce(testing::Invoke([&]() {
                ping1.set_value(); // notify first ping
            }))
            .WillOnce(testing::Invoke([&]() {
                ping2.set_value(); // notify second ping
            }));
        logger.log(engine, "stub");

        std::atomic_bool stopped_{false};
        auto worker = std::thread([&]() {
            while (!stopped_.load()) {
                logger.log(engine, "stub");
            }
        });
        ping1.get_future().get();
        ping2.get_future().get();

        stopped_ = true;
        worker.join();
    }

}
