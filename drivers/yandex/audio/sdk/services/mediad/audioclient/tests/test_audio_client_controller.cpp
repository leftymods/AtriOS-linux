#include <yandex_io/services/mediad/audioclient/audio_client_controller.h>

#include <yandex_io/libs/audio_player/gstreamer/gstreamer.h>
#include <yandex_io/libs/audio_player/gstreamer/gstreamer_audio_player.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/protobuf_utils/proto_trace.h>
#include <yandex_io/protos/model_objects.pb.h>
#include <yandex_io/tests/testlib/test_utils.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <library/cpp/testing/unittest/registar.h>

#include <future>

using namespace quasar;
using namespace quasar::gstreamer;
using namespace quasar::TestUtils;

class TestEventListener: public AudioEventListener {
public:
    using MessageHandler = std::function<void(const proto::AudioClientEvent& event)>;
    MessageHandler onEvent;

    void onAudioEvent(const proto::AudioClientEvent& event) override {
        __PROTOTRACE("TestEventListener", event);
        if (onEvent) {
            onEvent(event);
        }
    }
};

class Fixture: public QuasarUnitTestFixture {
public:
    YandexIO::Configuration::TestGuard testGuard;

    using Base = QuasarUnitTestFixture;

    void SetUp(NUnitTest::TTestContext& context) override {
        Base::SetUp(context);

        auto& config = getDeviceForTests()->configuration()->getMutableConfig(testGuard);
        config["audioclient"]["gogol"]["senderQueueSize"] = 15;
        config_ = config;

        YIO_LOG_DEBUG("Fixture initialized");
    }

    void TearDown(NUnitTest::TTestContext& context) override {
        Base::TearDown(context);
    }

    std::unique_ptr<AudioClientController> makeController(std::shared_ptr<AudioEventListener> listener) {
        return std::make_unique<AudioClientController>(getDeviceForTests(), ipcFactoryForTests(), nullptr, nullptr, nullptr,
                                                       std::make_shared<GstreamerAudioPlayerFactory>(ensureGstreamerInitialized()),
                                                       std::move(listener), config_["audioclient"]["controller"]);
    }

    static proto::AudioPlayerDescriptor getPlayerDescriptor() {
        proto::AudioPlayerDescriptor result;
        result.set_type(proto::AudioPlayerDescriptor::AUDIO);
        result.set_player_id(quasar::makeUUID());
        result.set_stream_id("test_audio_id");
        return result;
    }

    static proto::MediaRequest getPlayRequest() {
        return getPlayRequest("test_audio_id");
    }

    static proto::MediaRequest getPlayRequest(const std::string id) {
        auto playRequest = proto::MediaRequest();
        playRequest.mutable_play_audio()->set_id(TString(id));
        playRequest.mutable_play_audio()->set_url("test_audio_url");
        playRequest.mutable_play_audio()->set_initial_offset_ms(100L);
        playRequest.mutable_play_audio()->set_format(proto::Audio_Format_MP3);
        playRequest.mutable_player_descriptor()->CopyFrom(getPlayerDescriptor());
        return playRequest;
    }

    static proto::MediaRequest getPauseRequest() {
        auto pauseRequest = proto::MediaRequest();
        pauseRequest.mutable_pause();
        pauseRequest.mutable_player_descriptor()->CopyFrom(getPlayerDescriptor());
        return pauseRequest;
    }

    static proto::MediaRequest getCleanRequest() {
        auto cleanRequest = proto::MediaRequest();
        cleanRequest.mutable_clean_players();
        cleanRequest.mutable_player_descriptor()->CopyFrom(getPlayerDescriptor());
        return cleanRequest;
    }

    static proto::MediaRequest getResumeRequest() {
        auto resumeRequest = proto::MediaRequest();
        resumeRequest.set_resume(proto::MediaRequest::AUDIO);
        resumeRequest.mutable_player_descriptor()->CopyFrom(getPlayerDescriptor());
        return resumeRequest;
    }

    SteadyConditionVariable condVar_;
    std::mutex stateMutex_;
    Json::Value config_;
};

Y_UNIT_TEST_SUITE_F(audioclientcontroller, Fixture) {
    Y_UNIT_TEST(shouldReportPlayingState) {
        std::string playingAudioId;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            if (event.event() == proto::AudioClientEvent::STATE_CHANGED) {
                if (event.state() == proto::AudioClientState::PLAYING) {
                    playingAudioId = event.audio().id();
                    condVar_.notify_all();
                }
            }
        };

        auto controller = makeController(listener);
        YIO_LOG_INFO("Trying to play...");
        controller->handleMediaRequest(getPlayRequest());
        waitUntil(condVar_, stateMutex_, [&]() { return "test_audio_id" == playingAudioId; });
        YIO_LOG_INFO("Test audio started to play");
    }

    Y_UNIT_TEST(shouldReportStoppedStateByCommand) {
        bool audioStarted = false;
        bool audioStopped = false;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            if (event.event() == proto::AudioClientEvent::STATE_CHANGED) {
                if (event.state() == proto::AudioClientState::PLAYING) {
                    audioStarted = true;
                } else if (event.state() == proto::AudioClientState::STOPPED) {
                    audioStopped = true;
                }
            }
            condVar_.notify_all();
        };

        auto controller = makeController(listener);
        YIO_LOG_INFO("Trying to play...");
        controller->handleMediaRequest(getPlayRequest());
        waitUntil(condVar_, stateMutex_, [&]() { return audioStarted; });
        YIO_LOG_INFO("Test audio started to play");

        YIO_LOG_INFO("Trying to stop via clear...");
        controller->handleMediaRequest(getCleanRequest());
        waitUntil(condVar_, stateMutex_, [&]() { return audioStopped; });
        YIO_LOG_INFO("Test audio stopped");
    }

    Y_UNIT_TEST(shouldReportStoppedStateByDestructing) {
        bool audioStarted = false;
        bool audioStopped = false;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            if (event.event() == proto::AudioClientEvent::STATE_CHANGED) {
                if (event.state() == proto::AudioClientState::PLAYING) {
                    audioStarted = true;
                } else if (event.state() == proto::AudioClientState::STOPPED) {
                    audioStopped = true;
                }
            }
            condVar_.notify_all();
        };

        auto controller = makeController(listener);
        YIO_LOG_INFO("Trying to play...");
        controller->handleMediaRequest(getPlayRequest());
        waitUntil(condVar_, stateMutex_, [&]() { return audioStarted; });
        YIO_LOG_INFO("Test audio started to play");

        YIO_LOG_INFO("Trying to reset audio client...");
        controller->resetAllClients();
        waitUntil(condVar_, stateMutex_, [&]() { return audioStopped; });
        YIO_LOG_INFO("Test audio stopped");
    }

    Y_UNIT_TEST(shouldReportPlayingStateAfterResume) {
        bool audioStarted = false;
        bool audioPaused = false;
        bool audioResumed = false;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            if (event.event() == proto::AudioClientEvent::STATE_CHANGED) {
                if (event.state() == proto::AudioClientState::PLAYING) {
                    audioStarted = true;
                    if (audioPaused) {
                        audioResumed = true;
                    }
                } else if (event.state() == proto::AudioClientState::PAUSED) {
                    audioPaused = true;
                }
            }
            condVar_.notify_all();
        };

        auto controller = makeController(listener);
        YIO_LOG_INFO("Trying to play...");
        controller->handleMediaRequest(getPlayRequest());
        waitUntil(condVar_, stateMutex_, [&]() { return audioStarted; });
        YIO_LOG_INFO("Test audio started to play");

        YIO_LOG_INFO("Trying to pause...");
        controller->handleMediaRequest(getPauseRequest());
        waitUntil(condVar_, stateMutex_, [&]() { return audioPaused; });
        YIO_LOG_INFO("Test audio paused");

        YIO_LOG_INFO("Trying to resume...");
        controller->handleMediaRequest(getResumeRequest());
        waitUntil(condVar_, stateMutex_, [&]() { return audioResumed; });
        YIO_LOG_INFO("Test audio resumed");
    }
}
