#include <yandex_io/services/mediad/audioclient/audio_client_controller.h>

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <library/cpp/testing/gmock_in_unittest/gmock.h>
#include <library/cpp/testing/unittest/registar.h>

using namespace quasar;
using testing::ByMove;
using testing::Return;

namespace {

    class MockAudioPlayerFactory: public AudioPlayerFactory {
    public:
        MOCK_METHOD(std::unique_ptr<AudioPlayer>, createPlayer, (const AudioPlayer::Params& params), (override));
    };

    class MockEventListener: public AudioEventListener {
    public:
        MOCK_METHOD(void, onAudioEvent, (const proto::AudioClientEvent&), (override));
    };

    class MockAudioPlayer: public AudioPlayer {
    public:
        MockAudioPlayer(const Params& params)
            : AudioPlayer(params)
        {
        }
        MOCK_METHOD(bool, playAsync, (), (override));
        MOCK_METHOD(bool, replayAsync, (), (override));
        MOCK_METHOD(bool, pause, (), (override));
        MOCK_METHOD(bool, seek, (int), (override));
        MOCK_METHOD(bool, stop, (), (override));
        MOCK_METHOD(bool, playMultiroom, (std::chrono::nanoseconds, std::chrono::nanoseconds), (override));
        MOCK_METHOD(bool, isPlaying, (), (const, override));
        MOCK_METHOD(bool, startBuffering, (), (override));
        MOCK_METHOD(bool, setVolume, (double), (override));
        MOCK_METHOD(Channel, channel, (), (override));
        MOCK_METHOD(bool, setChannel, (Channel), (override));
        MOCK_METHOD(const std::vector<Format>&, supportedFormats, (), (const, override));
    };

    proto::AudioPlayerDescriptor makePlayerDescriptor() {
        proto::AudioPlayerDescriptor result;
        result.set_type(proto::AudioPlayerDescriptor::AUDIO);
        result.set_player_id(quasar::makeUUID());
        result.set_stream_id("test_audio_id");
        return result;
    }

    MATCHER(NoNormalization, "description") {
        const auto& params = arg;
        const auto& normalization = params.normalization();
        return normalization == std::nullopt;
    }
    MATCHER_P3(VerifyNormalization, tp, loudness, targetLufs, "description") {
        const auto& params = arg;
        const auto& normalization = params.normalization();
        if (normalization == std::nullopt) {
            *result_listener << "Normalization should be set up\n";
            return false;
        }

        *result_listener << "Input Values: tp: " << normalization->truePeak << ", loudness: " << normalization->integratedLoudness << ", tagetLufs" << normalization->targetLufs << '\n';

        *result_listener << "Expected Values: tp: " << tp << ", loudness: " << loudness << ", tagetLufs" << targetLufs << '\n';

        const auto doublesEquals = [](double left, double right) -> bool {
            return std::abs(left - right) < std::numeric_limits<double>::epsilon();
        };
        if (!doublesEquals(normalization->truePeak, tp)) {
            return false;
        }

        if (!doublesEquals(normalization->integratedLoudness, loudness)) {
            return false;
        }
        return doublesEquals(normalization->targetLufs, targetLufs);
    }

} // namespace

Y_UNIT_TEST_SUITE_F(TestAudioClientParams, QuasarUnitTestFixture) {
    Y_UNIT_TEST(TestNormalization) {
        using Normalization = AudioPlayer::Params::Normalization;
        const auto device = getDeviceForTests();
        const auto listener = std::make_shared<MockEventListener>();
        const auto factory = std::make_shared<MockAudioPlayerFactory>();
        const auto descriptor = makePlayerDescriptor();

        YandexIO::DeviceContext context(ipcFactoryForTests());
        const Json::Value playbackParams;
        const Json::Value extraPlaybackParams;

        {
            // no normalization by default
            Json::Value customConfig;
            proto::Audio audio;
            EXPECT_CALL(*factory, createPlayer(NoNormalization())).WillOnce(Return(ByMove(std::make_unique<MockAudioPlayer>(AudioPlayer::Params{}))));
            AudioClient client(device, context, nullptr, factory, listener,
                               playbackParams, extraPlaybackParams, customConfig,
                               descriptor, audio, std::make_shared<gogol::NullGogolSession>(),
                               std::make_shared<YandexIO::SpectrumProvider>(ipcFactoryForTests()), true);
        }

        {
            // Do not take normalization from directive is useNormalization setup to false
            Json::Value customConfig;
            customConfig["useNormalization"] = false;
            proto::Audio audio;
            audio.mutable_normalization()->set_true_peak(1.0);
            audio.mutable_normalization()->set_integrated_loudness(2.0);
            EXPECT_CALL(*factory, createPlayer(NoNormalization())).WillOnce(Return(ByMove(std::make_unique<MockAudioPlayer>(AudioPlayer::Params{}))));
            AudioClient client(device, context, nullptr, factory, listener,
                               playbackParams, extraPlaybackParams, customConfig,
                               descriptor, audio, std::make_shared<gogol::NullGogolSession>(),
                               std::make_shared<YandexIO::SpectrumProvider>(ipcFactoryForTests()), true);
        }

        {
            // Do not take normalization from system_confog is useNormalization is setup to false
            Json::Value customConfig;
            customConfig["useNormalization"] = false;
            proto::Audio audio;
            customConfig["normalization"]["truePeak"] = 1.0;
            customConfig["normalization"]["integratedLoudness"] = 2.0;
            EXPECT_CALL(*factory, createPlayer(NoNormalization())).WillOnce(Return(ByMove(std::make_unique<MockAudioPlayer>(AudioPlayer::Params{}))));
            AudioClient client(device, context, nullptr, factory, listener,
                               playbackParams, extraPlaybackParams, customConfig,
                               descriptor, audio, std::make_shared<gogol::NullGogolSession>(),
                               std::make_shared<YandexIO::SpectrumProvider>(ipcFactoryForTests()), true);
        }

        {
            // check normalization from directive is applied by default
            Json::Value customConfig;
            proto::Audio audio;
            audio.mutable_normalization()->set_true_peak(1.0);
            audio.mutable_normalization()->set_integrated_loudness(2.0);
            EXPECT_CALL(*factory, createPlayer(VerifyNormalization(1.0, 2.0, Normalization::DEFAULT_TARGET_LUFS))).WillOnce(Return(ByMove(std::make_unique<MockAudioPlayer>(AudioPlayer::Params{}))));
            AudioClient client(device, context, nullptr, factory, listener,
                               playbackParams, extraPlaybackParams, customConfig,
                               descriptor, audio, std::make_shared<gogol::NullGogolSession>(),
                               std::make_shared<YandexIO::SpectrumProvider>(ipcFactoryForTests()), true);
        }

        {
            // check normalization from system_config  is applied when "useNormalization" is setup to true in system_config
            Json::Value customConfig;
            proto::Audio audio;
            customConfig["useNormalization"] = true;
            customConfig["normalization"]["truePeak"] = 3.0;
            customConfig["normalization"]["integratedLoudness"] = 4.0;
            EXPECT_CALL(*factory, createPlayer(VerifyNormalization(3.0, 4.0, Normalization::DEFAULT_TARGET_LUFS))).WillOnce(Return(ByMove(std::make_unique<MockAudioPlayer>(AudioPlayer::Params{}))));
            AudioClient client(device, context, nullptr, factory, listener,
                               playbackParams, extraPlaybackParams, customConfig,
                               descriptor, audio, std::make_shared<gogol::NullGogolSession>(),
                               std::make_shared<YandexIO::SpectrumProvider>(ipcFactoryForTests()), true);
        }

        {
            // check that system_config Normalization values wins when directive values are also set up
            Json::Value customConfig;
            customConfig["useNormalization"] = true;
            customConfig["normalization"]["truePeak"] = 5.0;
            customConfig["normalization"]["integratedLoudness"] = 6.0;
            proto::Audio audio;
            audio.mutable_normalization()->set_true_peak(7.0);
            audio.mutable_normalization()->set_integrated_loudness(8.0);
            EXPECT_CALL(*factory, createPlayer(VerifyNormalization(5.0, 6.0, Normalization::DEFAULT_TARGET_LUFS))).WillOnce(Return(ByMove(std::make_unique<MockAudioPlayer>(AudioPlayer::Params{}))));
            AudioClient client(device, context, nullptr, factory, listener,
                               playbackParams, extraPlaybackParams, customConfig,
                               descriptor, audio, std::make_shared<gogol::NullGogolSession>(),
                               std::make_shared<YandexIO::SpectrumProvider>(ipcFactoryForTests()), true);
        }

        {
            // check custom target_lufs from config with directive
            Json::Value customConfig;
            customConfig["useNormalization"] = true;
            customConfig["normalizationTargetLufs"] = -12.0;
            proto::Audio audio;
            audio.mutable_normalization()->set_true_peak(7.0);
            audio.mutable_normalization()->set_integrated_loudness(8.0);
            EXPECT_CALL(*factory, createPlayer(VerifyNormalization(7.0, 8.0, -12.0))).WillOnce(Return(ByMove(std::make_unique<MockAudioPlayer>(AudioPlayer::Params{}))));
            AudioClient client(device, context, nullptr, factory, listener,
                               playbackParams, extraPlaybackParams, customConfig,
                               descriptor, audio, std::make_shared<gogol::NullGogolSession>(),
                               std::make_shared<YandexIO::SpectrumProvider>(ipcFactoryForTests()), true);
        }

        {
            // check custom target_lufs from config with normalization values from config
            Json::Value customConfig;
            customConfig["useNormalization"] = true;
            customConfig["normalizationTargetLufs"] = -12.0;
            customConfig["normalization"]["truePeak"] = 11.0;
            customConfig["normalization"]["integratedLoudness"] = 12.0;
            proto::Audio audio;
            EXPECT_CALL(*factory, createPlayer(VerifyNormalization(11.0, 12.0, -12.0))).WillOnce(Return(ByMove(std::make_unique<MockAudioPlayer>(AudioPlayer::Params{}))));
            AudioClient client(device, context, nullptr, factory, listener,
                               playbackParams, extraPlaybackParams, customConfig,
                               descriptor, audio, std::make_shared<gogol::NullGogolSession>(),
                               std::make_shared<YandexIO::SpectrumProvider>(ipcFactoryForTests()), true);
        }
    }

} // suite
