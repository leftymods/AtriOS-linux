#include <yandex_io/modules/audio_input/vqe/engine_factory/vqe_engine_factory.h>

#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <library/cpp/testing/unittest/registar.h>

class VQEEngineMock: public YandexIO::VQEEngine {
public:
    explicit VQEEngineMock(double saved)
        : saved_(saved)
    {
    }

    void process(const std::vector<float>& /*inputMic*/,
                 const std::vector<float>& /*inputSpk*/,
                 double& doaAngle, bool& /*speechDetected*/) override {
        doaAngle = saved_;
    };

    ChannelCount getInputChannelCount() const override {
        return {0u, 0u};
    }

    size_t getOutputChannelCount(YandexIO::ChannelData::Type /*chType*/) const override {
        return 0;
    }

    std::span<const float> getOutputChannelData(YandexIO::ChannelData::Type /*chType*/, size_t /*channelId*/) const override {
        return result_;
    }

private:
    double saved_;
    std::vector<float> result_;
};

Y_UNIT_TEST_SUITE(TestVqeEngineFactory) {

    Y_UNIT_TEST_F(simple, QuasarUnitTestFixture) {
        YandexIO::VqeEngineFactory factory;
        factory.addEngineType("1", [](const Json::Value& /*config*/, const std::string& /*deviceType*/) {
            return std::make_shared<VQEEngineMock>(1.0);
        });
        factory.addEngineType("2", [](const Json::Value& /*config*/, const std::string& /*deviceType*/) {
            return std::make_shared<VQEEngineMock>(2.0);
        });

        std::vector<float> mic;
        std::vector<float> spk;
        double doa;
        bool unused;

        Json::Value config;
        config["VQEtype"] = "1";
        auto engine1 = factory.createEngine(config, "preset");
        config["VQEtype"] = "2";
        auto engine2 = factory.createEngine(config, "preset");

        engine1->process(mic, spk, doa, unused);
        UNIT_ASSERT_DOUBLES_EQUAL(doa, 1.0, 0.0001);

        engine2->process(mic, spk, doa, unused);
        UNIT_ASSERT_DOUBLES_EQUAL(doa, 2.0, 0.0001);
    }

    Y_UNIT_TEST_F(addSameVqeTypeFail, QuasarUnitTestFixture) {
        YandexIO::VqeEngineFactory factory;
        UNIT_ASSERT_NO_EXCEPTION(factory.addEngineType("1", [](const Json::Value& /*config*/, const std::string& /*deviceType*/) {
            return std::make_shared<VQEEngineMock>(1.0);
        }));
        UNIT_ASSERT_EXCEPTION(factory.addEngineType("1", [](const Json::Value& /*config*/, const std::string& /*deviceType*/) {
            return std::make_shared<VQEEngineMock>(1.0);
        }), std::runtime_error);
    }

    Y_UNIT_TEST_F(createUnknownVqeTypeThrows, QuasarUnitTestFixture) {
        YandexIO::VqeEngineFactory factory;
        factory.addEngineType("1", [](const Json::Value& /*config*/, const std::string& /*deviceType*/) {
            return std::make_shared<VQEEngineMock>(1.0);
        });
        Json::Value config;
        config["VQEtype"] = "2";
        UNIT_ASSERT_EXCEPTION(factory.createEngine(config, "preset"), std::runtime_error);
    }

}
