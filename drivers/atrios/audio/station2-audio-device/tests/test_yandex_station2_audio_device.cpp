#include "smart_devices/platforms/yandexstation_2/platformd/audio_device/yandex_station2_audio_device.h"

#include <yandex_io/libs/telemetry/mock/mock_telemetry.h>
#include <yandex_io/modules/audio_input/vqe/controller/vqe_controller.h>
#include <yandex_io/modules/audio_input/vqe/engine/vqe_engine.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <library/cpp/testing/unittest/registar.h>

namespace {
    Json::Value makeVqeConfig() {
        Json::Value config;
        config = Json::Value();
        config["name"] = "vqe";
        config["periodSize"] = 1024;
        config["spkChannels"] = 6;
        config["preset"] = "yandexstation_2_rev1";
        config["VQEtype"] = "yandex";
        config["omniMode"] = true;
        return config;
    }

    Json::Value makeConfig() {
        Json::Value config;
        config["affinityMask"] = 0;
        config["mainChannel"] = "vqe";
        config["capturedChannels"] = Json::Value();
        config["capturedChannels"][0] = "*";
        config["micConfig"] = Json::Value();
        config["micConfig"]["periodSize"] = 3072;
        config["micConfig"]["periodCount"] = 4;
        config["micConfig"]["cardNumber"] = 0;
        config["micConfig"]["deviceNumber"] = 1;
        config["micConfig"]["micChannels"] = 8;
        config["micConfig"]["spkChannels"] = 0;
        config["micConfig"]["inRate"] = 48000;
        config["micConfig"]["sampleSize"] = 2;
        config["spkConfig"] = Json::Value();
        config["spkConfig"]["periodSize"] = 3072;
        config["spkConfig"]["periodCount"] = 4;
        config["spkConfig"]["cardNumber"] = 0;
        config["spkConfig"]["deviceNumber"] = 0;
        config["spkConfig"]["micChannels"] = 0;
        config["spkConfig"]["spkChannels"] = 2;
        config["spkConfig"]["inRate"] = 48000;
        config["spkConfig"]["sampleSize"] = 2;
        config["loopbackConfig"] = Json::Value();
        config["loopbackConfig"]["periodSize"] = 3072;
        config["loopbackConfig"]["periodCount"] = 4;
        config["loopbackConfig"]["cardNumber"] = 1;
        config["loopbackConfig"]["deviceNumber"] = 1;
        config["loopbackConfig"]["micChannels"] = 0;
        config["loopbackConfig"]["spkChannels"] = 2;
        config["loopbackConfig"]["inRate"] = 48000;
        config["loopbackConfig"]["sampleSize"] = 2;
        config["hdmiLoopbackConfig"] = Json::Value();
        config["hdmiLoopbackConfig"]["periodSize"] = 3072;
        config["hdmiLoopbackConfig"]["periodCount"] = 4;
        config["hdmiLoopbackConfig"]["cardNumber"] = 1;
        config["hdmiLoopbackConfig"]["deviceNumber"] = 0;
        config["hdmiLoopbackConfig"]["micChannels"] = 0;
        config["hdmiLoopbackConfig"]["spkChannels"] = 2;
        config["hdmiLoopbackConfig"]["inRate"] = 48000;
        config["hdmiLoopbackConfig"]["sampleSize"] = 2;
        config["yandex_vqe"] = makeVqeConfig();
        return config;
    }

    const int MIC_CHANNELS = 8;
    const int MIC_BUFFER_SIZE = MIC_CHANNELS * 1024;
    const int CHANNELS = 2;
    const int BUFFER_SIZE = CHANNELS * 1024;

    class MockVQEEngine: public YandexIO::VQEEngine {
    public:
        MockVQEEngine()
            : data_(1024, 0)
        {
        }

        void process(
            const std::vector<float>& /*inputMic*/,
            const std::vector<float>& /*nputSpk*/,
            double& /*doaAngle*/,
            bool& /*speechDetected*/) override {
        }

        YandexIO::VQEEngine::ChannelCount getInputChannelCount() const override {
            return {2, 6};
        }
        size_t getOutputChannelCount(YandexIO::ChannelData::Type type) const override {
            // YandexVqeAudioDeviceBase::captureVqeChannels
            if (YandexIO::ChannelData::Type::VQE == type) {
                return 1;
            }
            return 0;
        }
        std::span<const float> getOutputChannelData(YandexIO::ChannelData::Type /*type*/, size_t /*index*/) const override {
            // YandexVqeAudioDeviceBase::captureVqeChannels
            return data_;
        }

        void setOmniMode(bool /* omniMode */) override {
        }

        void setSpeakerVolume(int /* volume */) override {
        }

        int getPeriodSize() const override {
            return 0;
        }

        std::optional<int> getFeedbackShift() const override {
            return {};
        }

        std::optional<float> getFeedbackShiftCorrelation() const override {
            return {};
        }

        YandexIO::FeedbackSource hardwareSyncTarget() const override {
            return YandexIO::FeedbackSource::HW;
        }

    private:
        std::vector<float> data_;
    };
} // namespace

namespace quasar {
    class YandexStation2AudioDeviceFixture: public QuasarUnitTestFixtureWithoutIpc {
    public:
        using Base = NUnitTest::TBaseFixture;

        void SetUp(NUnitTest::TTestContext& context) override {
            Base::SetUp(context);

            Json::Value config = makeConfig();
            Json::Value vqeConfig = makeVqeConfig();
            const bool useHwSync = false;

            AudioReaders audioReaders(
                {std::make_unique<ZerosAudioReader>(CHANNELS, BUFFER_SIZE),
                 std::make_unique<ZerosAudioReader>(MIC_CHANNELS, MIC_BUFFER_SIZE),
                 std::make_unique<ZerosAudioReader>(CHANNELS, BUFFER_SIZE),
                 std::make_unique<ZerosAudioReader>(CHANNELS, BUFFER_SIZE)},
                useHwSync,
                YandexIO::FeedbackSource::HW);

            YandexIO::VqeController::EnginesList allEngines;
            allEngines.emplace_back("yandex", [](const Json::Value& /*config*/, const std::string& /*deviceType*/) {
                return std::make_shared<MockVQEEngine>();
            });

            auto vqeController = std::make_shared<YandexIO::VqeController>("yandex", allEngines);
            vqeController->setEngine(vqeConfig);

            audioDevice_ = std::make_unique<YandexStation2AudioDevice>(
                config,
                "deviceType",
                "vqeType",
                false,
                std::make_unique<YandexIO::MockTelemetry>(),
                vqeController,
                std::move(audioReaders));
        }

        void TearDown(NUnitTest::TTestContext& context) override {
            Base::TearDown(context);
            audioDevice_.reset();
        }

    protected:
        std::unique_ptr<YandexStation2AudioDevice> audioDevice_;
    };

    Y_UNIT_TEST_SUITE_F(TestYandexStation2AudioDevice, YandexStation2AudioDeviceFixture) {
        Y_UNIT_TEST(TestAudioDeviceCreated) {
            UNIT_ASSERT(audioDevice_);
        }

        Y_UNIT_TEST(TestAudioDeviceNotBlocked) {
            audioDevice_->start();
            audioDevice_->capture();
        }
    }
} // namespace quasar
