#include <smart_devices/libs/pcba/pcba_ids.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/telemetry/null/null_metrica.h>

#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <library/cpp/testing/unittest/registar.h>
#include <library/cpp/testing/gmock_in_unittest/gmock.h>

#include <util/system/tempfile.h>

#include <future>

using namespace quasar;

namespace {

    class TelemetryMock: public NullMetrica {
    public:
        MOCK_METHOD(void, reportEvent, (const std::string&, const std::string&, ITelemetry::Flags), (override));
    };

    Json::Value makeBp() {
        Json::Value json;
        json["name"] = "bp";
        json["struct_version"] = 1;
        json["hex"] = "0x30 0x31 0x59 0x30 0x30 0x30 0x38 0x46 0x44 0x30 0x32 0x30 0x32 0x30 0x31 0x30 0x31 0x32 0x30 0x30 0x39 0x31 0x35 0x30 0x30 0x34 0x35 0x30 0x44 0x42 0x35 0x30";
        json["board_code"] = 2;
        json["model"] = 21;
        json["date"] = "20.09.15";
        json["revision"]["hi"] = 1;
        json["revision"]["mid"] = 2;
        json["revision"]["lo"] = 3;
        return json;
    }

    Json::Value makeSom() {
        Json::Value json;
        json["name"] = "som";
        json["struct_version"] = 1;
        json["hex"] = "0x30 0x31 0x59 0x30 0x30 0x30 0x38 0x46 0x44 0x30 0x31 0x30 0x32 0x30 0x31 0x30 0x31 0x32 0x30 0x30 0x38 0x31 0x35 0x30 0x30 0x31 0x38 0x36 0x36 0x31 0x39 0x42";
        json["board_code"] = 1;
        json["model"] = 21;
        json["date"] = "20.08.15";
        json["revision"]["hi"] = 2;
        json["revision"]["mid"] = 1;
        json["revision"]["lo"] = 1;
        return json;
    }

} // namespace

Y_UNIT_TEST_SUITE_F(PcbaIdsReaderTest, QuasarUnitTestFixtureWithoutIpc) {
    Y_UNIT_TEST(FileDoNotExist) {
        UNIT_ASSERT_EQUAL(loadPCBAIds("not_a_file"), std::nullopt);
    }

    Y_UNIT_TEST(NotJson) {
        TTempFileHandle file("testPcba");
        const std::string data = "not a json";
        file.Write(data.c_str(), data.size());
        file.Flush();

        UNIT_ASSERT_EQUAL(loadPCBAIds("testPcba"), std::nullopt);
    }

    Y_UNIT_TEST(NotArray) {
        TTempFileHandle file("testPcba");
        const std::string data = jsonToString(Json::nullValue);
        file.Write(data.c_str(), data.size());
        file.Flush();

        UNIT_ASSERT_EQUAL(loadPCBAIds("testPcba"), std::nullopt);
    }

    Y_UNIT_TEST(SuccessCase) {
        TTempFileHandle file("testPcba");
        {
            Json::Value json = Json::arrayValue;
            json[0] = makeBp();
            json[1] = makeSom();
            const auto content = jsonToString(json);
            file.Write(content.c_str(), content.size());
            file.Flush();
        }

        const auto pcbaIdsOpt = loadPCBAIds("testPcba");
        UNIT_ASSERT(pcbaIdsOpt.has_value());
        const auto& pcbaIds = *pcbaIdsOpt;

        UNIT_ASSERT(pcbaIds.hasPCBA("bp"));
        UNIT_ASSERT(pcbaIds.hasPCBA("som"));

        const auto& bp = pcbaIds.getPCBA("bp");
        UNIT_ASSERT(!bp.hasError());
        UNIT_ASSERT_VALUES_EQUAL(bp.name, "bp");
        UNIT_ASSERT_VALUES_EQUAL(bp.structVersion, 1);
        UNIT_ASSERT_VALUES_EQUAL(bp.hex, "0x30 0x31 0x59 0x30 0x30 0x30 0x38 0x46 0x44 0x30 0x32 0x30 0x32 0x30 0x31 0x30 0x31 0x32 0x30 0x30 0x39 0x31 0x35 0x30 0x30 0x34 0x35 0x30 0x44 0x42 0x35 0x30");
        UNIT_ASSERT_VALUES_EQUAL(bp.boardCode, 2);
        UNIT_ASSERT_VALUES_EQUAL(bp.productModel, 21);
        UNIT_ASSERT_VALUES_EQUAL(bp.date, "20.09.15");
        UNIT_ASSERT_VALUES_EQUAL(bp.revision.hi, 1);
        UNIT_ASSERT_VALUES_EQUAL(bp.revision.mid, 2);
        UNIT_ASSERT_VALUES_EQUAL(bp.revision.lo, 3);

        const auto& som = pcbaIds.getPCBA("som");
        UNIT_ASSERT(!som.hasError());
        UNIT_ASSERT_VALUES_EQUAL(som.name, "som");
        UNIT_ASSERT_VALUES_EQUAL(som.structVersion, 1);
        UNIT_ASSERT_VALUES_EQUAL(som.hex, "0x30 0x31 0x59 0x30 0x30 0x30 0x38 0x46 0x44 0x30 0x31 0x30 0x32 0x30 0x31 0x30 0x31 0x32 0x30 0x30 0x38 0x31 0x35 0x30 0x30 0x31 0x38 0x36 0x36 0x31 0x39 0x42");
        UNIT_ASSERT_VALUES_EQUAL(som.boardCode, 1);
        UNIT_ASSERT_VALUES_EQUAL(som.productModel, 21);
        UNIT_ASSERT_VALUES_EQUAL(som.date, "20.08.15");
        UNIT_ASSERT_VALUES_EQUAL(som.revision.hi, 2);
        UNIT_ASSERT_VALUES_EQUAL(som.revision.mid, 1);
        UNIT_ASSERT_VALUES_EQUAL(som.revision.lo, 1);
    }

    Y_UNIT_TEST(OnePcbWithError) {
        TTempFileHandle file("testPcba");
        {
            Json::Value json = Json::arrayValue;
            json[0] = makeBp();
            json[1] = makeSom();
            // pcba with error
            json[2]["name"] = "led_display";
            json[2]["error"] = "empty eeprom";
            const auto content = jsonToString(json);
            file.Write(content.c_str(), content.size());
            file.Flush();
        }

        const auto pcbaIdsOpt = loadPCBAIds("testPcba");
        UNIT_ASSERT(pcbaIdsOpt.has_value());
        const auto& pcbaIds = *pcbaIdsOpt;

        // all pcba infos should be read
        UNIT_ASSERT(pcbaIds.hasPCBA("bp"));
        UNIT_ASSERT(pcbaIds.hasPCBA("som"));
        UNIT_ASSERT(pcbaIds.hasPCBA("led_display"));

        // only led_display should have error
        UNIT_ASSERT(!pcbaIds.getPCBA("bp").hasError());
        UNIT_ASSERT(!pcbaIds.getPCBA("bp").hasError());
        UNIT_ASSERT(pcbaIds.getPCBA("led_display").hasError());

        UNIT_ASSERT_VALUES_EQUAL(pcbaIds.getPCBA("led_display").error.value(), "empty eeprom");
    }

    Y_UNIT_TEST(PCBAInfoToJson) {
        {
            PCBAInfo info;
            info.name = "test1";
            info.error = "test error";
            const auto json = PCBAInfo::toJson(info);
            UNIT_ASSERT(json.isMember("name"));
            UNIT_ASSERT_VALUES_EQUAL(json["name"].asString(), "test1");
            UNIT_ASSERT_VALUES_EQUAL(json["error"].asString(), "test error");
        }

        {
            PCBAInfo info;
            info.name = "test1";
            info.structVersion = 1;
            info.boardCode = 2;
            info.productModel = 3;
            info.hex = "0xFF 0x12";
            info.date = "15.07.2021";
            info.revision.hi = 4;
            info.revision.mid = 5;
            info.revision.lo = 6;

            const auto json = PCBAInfo::toJson(info);
            UNIT_ASSERT(json.isMember("name"));
            UNIT_ASSERT_VALUES_EQUAL(json["name"].asString(), "test1");
            UNIT_ASSERT(json.isMember("hex"));
            UNIT_ASSERT_VALUES_EQUAL(json["hex"].asString(), "0xFF 0x12");
            UNIT_ASSERT(json.isMember("date"));
            UNIT_ASSERT_VALUES_EQUAL(json["date"].asString(), "15.07.2021");
            UNIT_ASSERT(json.isMember("struct_version"));
            UNIT_ASSERT_VALUES_EQUAL(json["struct_version"].asInt(), 1);
            UNIT_ASSERT(json.isMember("board_code"));
            UNIT_ASSERT_VALUES_EQUAL(json["board_code"].asInt(), 2);
            UNIT_ASSERT(json.isMember("model"));
            UNIT_ASSERT_VALUES_EQUAL(json["model"].asInt(), 3);
            UNIT_ASSERT(json.isMember("revision"));
            UNIT_ASSERT(json["revision"].isMember("hi"));
            UNIT_ASSERT_VALUES_EQUAL(json["revision"]["hi"].asInt(), 4);
            UNIT_ASSERT(json["revision"].isMember("mid"));
            UNIT_ASSERT_VALUES_EQUAL(json["revision"]["mid"].asInt(), 5);
            UNIT_ASSERT(json["revision"].isMember("lo"));
            UNIT_ASSERT_VALUES_EQUAL(json["revision"]["lo"].asInt(), 6);
        }
    }

    Y_UNIT_TEST(PCBAIdsToJson) {
        Json::Value originalJson = Json::arrayValue;
        const auto bp = makeBp();
        const auto som = makeSom();
        originalJson[0] = bp;
        originalJson[1] = som;

        const PCBAIds ids(originalJson);

        const auto json = ids.toJson();
        UNIT_ASSERT(json.isArray());
        UNIT_ASSERT_VALUES_EQUAL(json.size(), 2);
        // NOTE: array order may change. it's okay
        for (Json::ArrayIndex i = 0; i < json.size(); ++i) {
            const auto& pcba = json[i];
            UNIT_ASSERT(pcba.isMember("name"));
            if (pcba["name"].asString() == "som") {
                UNIT_ASSERT_EQUAL(pcba, som);
            } else if (pcba["name"].asString() == "bp") {
                UNIT_ASSERT_EQUAL(pcba, bp);
            } else {
                UNIT_FAIL("Unexpected value in array");
            }
        }
    }

    Y_UNIT_TEST(TestPCBAIdsMetrics) {
        const auto telemetry = std::make_shared<TelemetryMock>();
        Json::Value payload;
        payload["test"] = "payload";

        const auto payloadStr = jsonToString(payload);
        std::promise<void> periodicMetricsPromise;

        using testing::_;
        EXPECT_CALL(*telemetry, reportEvent("pcbaIdsInfo", payloadStr, _))
            .Times(testing::AtLeast(3))
            .WillOnce(testing::Return())
            .WillOnce(testing::Return())
            .WillOnce(testing::Invoke([&]() {
                periodicMetricsPromise.set_value();
            }));

        PCBAIdsMetricSender sender(payload, telemetry, std::chrono::seconds(1));
        periodicMetricsPromise.get_future().get();
    }

} // suite
