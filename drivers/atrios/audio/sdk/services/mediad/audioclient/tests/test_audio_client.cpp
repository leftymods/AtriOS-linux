#include <yandex_io/services/mediad/audioclient/audio_client_controller.h>

#include <yandex_io/libs/audio_player/gstreamer/gstreamer.h>
#include <yandex_io/libs/audio_player/gstreamer/gstreamer_audio_player.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/protobuf_utils/proto_trace.h>
#include <yandex_io/protos/model_objects.pb.h>
#include <yandex_io/tests/testlib/test_utils.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>
#include <yandex_io/libs/protobuf_utils/json.h>

#include <library/cpp/cgiparam/cgiparam.h>
#include <library/cpp/string_utils/url/url.h>
#include <library/cpp/testing/unittest/registar.h>

#include <future>

using namespace quasar;
using namespace quasar::gstreamer;
using namespace quasar::TestUtils;

namespace {

    std::string removeVsidFromUrl(const std::string& url) {
        TStringBuf urlBase;
        TStringBuf query;
        TStringBuf fragment;
        SeparateUrlFromQueryAndFragment(url, urlBase, query, fragment);

        TCgiParameters params(query);

        params.Erase("vsid"); // remove vsid emplaced for Gogol

        std::stringstream ss;
        ss << urlBase;
        if (const auto paramsStr = params.Print(); !paramsStr.Empty()) {
            ss << '?' << paramsStr;
        }
        if (!fragment.empty()) {
            ss << '#' << fragment;
        }

        return ss.str();
    }

} // namespace

static constexpr const char* GOOD_PIPELINE = "souphttpsrc location={uri} ! decodebin use-buffering=true ! volume name=volume0 ! audioconvert ! audioresample ! fakesink";

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

class AudioClientFixture: public QuasarUnitTestFixture {
public:
    YandexIO::Configuration::TestGuard testGuard;

    using Base = QuasarUnitTestFixture;

    void SetUp(NUnitTest::TTestContext& context) override {
        Base::SetUp(context);

        config_ = getDeviceForTests()->configuration()->getMutableConfig(testGuard);
        playbackParams_ = config_["audioclient"]["controller"]["playbackParams"];
        deviceContext_ = std::make_unique<YandexIO::DeviceContext>(ipcFactoryForTests(), nullptr, false);

        YIO_LOG_DEBUG("Fixture initialized");
        YIO_LOG_DEBUG("playbackParams: " << jsonToString(playbackParams_));
    }

    void TearDown(NUnitTest::TTestContext& context) override {
        Base::TearDown(context);
    }

    std::unique_ptr<AudioClient> makePlayer(const std::shared_ptr<TestEventListener>& listener,
                                            const proto::AudioPlayerDescriptor& descriptor,
                                            const proto::Audio& audio) {
        return std::make_unique<AudioClient>(
            getDeviceForTests(),
            *deviceContext_,
            std::shared_ptr<const IAudioClockManager>{},
            std::make_shared<GstreamerAudioPlayerFactory>(ensureGstreamerInitialized()),
            listener,
            playbackParams_,
            extraPlaybackParams_,
            audioClientConfig_,
            descriptor,
            audio,
            std::make_shared<gogol::NullGogolSession>(),
            std::make_shared<YandexIO::SpectrumProvider>(ipcFactoryForTests()),
            true);
    }

    static proto::AudioPlayerDescriptor getDescriptor(const std::string& streamId, const std::string& playerId = quasar::makeUUID()) {
        proto::AudioPlayerDescriptor result;
        result.set_type(proto::AudioPlayerDescriptor::AUDIO);
        result.set_player_id(TString(playerId));
        result.set_stream_id(TString(streamId));
        return result;
    }

    static proto::Audio getAudio(const std::string& streamId, const std::string& url = "test_audio_url") {
        auto audio = proto::Audio();
        audio.set_id(TString(streamId));
        audio.set_url(TString(url));
        audio.set_format(proto::Audio::MP3);
        return audio;
    }

    SteadyConditionVariable condVar_;
    std::mutex stateMutex_;
    Json::Value config_;
    Json::Value playbackParams_;
    Json::Value extraPlaybackParams_;
    Json::Value audioClientConfig_;
    std::unique_ptr<YandexIO::DeviceContext> deviceContext_;
};

Y_UNIT_TEST_SUITE_F(audioclient, AudioClientFixture) {
    Y_UNIT_TEST(playingStateReported) {
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

        const auto streamId = "test_stream_id";
        YIO_LOG_INFO("Trying to play");
        auto player = makePlayer(listener, getDescriptor(streamId), getAudio(streamId));
        player->play(true, false, AudioPlayer::Channel::ALL, {});

        waitUntil(condVar_, stateMutex_, [&]() { return streamId == playingAudioId; });
        YIO_LOG_INFO("Started");
    }

    Y_UNIT_TEST(idleStateReportedFirst) {
        std::string playingAudioId;
        std::queue<proto::AudioClientEvent> receivedEvents;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            receivedEvents.push(event);
            if (event.event() == proto::AudioClientEvent::STATE_CHANGED) {
                if (event.state() == proto::AudioClientState::PLAYING) {
                    playingAudioId = event.audio().id();
                    condVar_.notify_all();
                }
            }
        };

        // play and wait until playing
        const auto streamId = "test_stream_id";
        YIO_LOG_INFO("Trying to play");
        auto player = makePlayer(listener, getDescriptor(streamId), getAudio(streamId));
        player->play(true, false, AudioPlayer::Channel::ALL, {});
        waitUntil(condVar_, stateMutex_, [&]() { return streamId == playingAudioId; });
        YIO_LOG_INFO("Started");

        // check that idle state was reported first
        {
            std::lock_guard<std::mutex> guard(stateMutex_);
            const auto first = receivedEvents.front();
            UNIT_ASSERT_VALUES_EQUAL(int(first.event()), int(proto::AudioClientEvent::STATE_CHANGED));
            UNIT_ASSERT_VALUES_EQUAL(int(first.state()), int(proto::AudioClientState::IDLE));
        }
    }

    Y_UNIT_TEST(stoppedStateReportedLast) {
        bool stoppedReceived = false;
        std::string playingAudioId;
        std::queue<proto::AudioClientEvent> receivedEvents;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            receivedEvents.push(event);
            if (event.event() == proto::AudioClientEvent::STATE_CHANGED) {
                if (event.state() == proto::AudioClientState::PLAYING) {
                    playingAudioId = event.audio().id();
                } else if (event.state() == proto::AudioClientState::STOPPED) {
                    stoppedReceived = true;
                }
                condVar_.notify_all();
            }
        };

        // play and wait until playing
        const auto streamId = "test_stream_id";
        YIO_LOG_INFO("Trying to play");
        auto player = makePlayer(listener, getDescriptor(streamId), getAudio(streamId));
        player->play(true, false, AudioPlayer::Channel::ALL, {});
        waitUntil(condVar_, stateMutex_, [&]() { return streamId == playingAudioId; });
        YIO_LOG_INFO("Started");

        // drop player. wait until it will be stopped
        YIO_LOG_INFO("Trying to destroy");
        player.reset();
        waitUntil(condVar_, stateMutex_, [&]() { return stoppedReceived; });
        YIO_LOG_INFO("Stopped");

        // sleep some time, to give a chance to receive more events on listener
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        YIO_LOG_INFO("Woke up");

        // check that stopped state was reported last
        {
            std::lock_guard<std::mutex> guard(stateMutex_);
            const auto last = receivedEvents.back();
            UNIT_ASSERT_VALUES_EQUAL(int(last.event()), int(proto::AudioClientEvent::STATE_CHANGED));
            UNIT_ASSERT_VALUES_EQUAL(int(last.state()), int(proto::AudioClientState::STOPPED));
        }
    }

    Y_UNIT_TEST(switchBetweenStates) {
        proto::AudioClientState lastReceivedState;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            lastReceivedState = event.state();
            condVar_.notify_all();
        };

        // play and wait until playing
        const auto streamId = "test_stream_id";
        YIO_LOG_INFO("Trying to play");
        auto player = makePlayer(listener, getDescriptor(streamId), getAudio(streamId));
        player->play(true, false, AudioPlayer::Channel::ALL, {});
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::PLAYING; });
        YIO_LOG_INFO("Started");

        // pause playback
        YIO_LOG_INFO("Trying to pause");
        player->pause();
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::PAUSED; });
        YIO_LOG_INFO("Paused");

        // resume playback
        YIO_LOG_INFO("Trying to resume");
        player->resume(true);
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::PLAYING; });
        YIO_LOG_INFO("Resumed");

        // stop playback
        YIO_LOG_INFO("Trying to stop via destroy");
        player.reset();
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::STOPPED; });
        YIO_LOG_INFO("Stopped");
    }

    Y_UNIT_TEST(failedStateReported) {
        proto::AudioClientState lastReceivedState;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            lastReceivedState = event.state();
            condVar_.notify_all();
        };

        // play with proper pipeline, but with wrong url - to get failed state
        playbackParams_["MP3"]["gstPipeline"] = GOOD_PIPELINE;
        const auto streamId = "test_stream_id";
        YIO_LOG_INFO("Trying to play");
        auto player = makePlayer(listener, getDescriptor(streamId), getAudio(streamId, "some_wrong_url"));
        player->play(true, false, AudioPlayer::Channel::ALL, {});
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::FAILED; });
        YIO_LOG_INFO("Failed");
    }

    Y_UNIT_TEST(doNotRecoverFromFailedState) {
        proto::AudioClientState lastReceivedState;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            lastReceivedState = event.state();
            condVar_.notify_all();
        };

        // play with proper pipeline, but with wrong url - to get failed state
        playbackParams_["MP3"]["gstPipeline"] = GOOD_PIPELINE;
        const auto streamId = "test_stream_id";
        YIO_LOG_INFO("Trying to play");
        auto player = makePlayer(listener, getDescriptor(streamId), getAudio(streamId, "some_wrong_url"));
        player->play(true, false, AudioPlayer::Channel::ALL, {});

        // sleep some time, to give a chance to receive more events on listener
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::FAILED; });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState != proto::AudioClientState::PLAYING; });
        YIO_LOG_INFO("Not playing 1");

        YIO_LOG_INFO("Try to recover by play");
        player->play(true, false, AudioPlayer::Channel::ALL, {});
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::FAILED; });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState != proto::AudioClientState::PLAYING; });
        YIO_LOG_INFO("Not playing 2");

        YIO_LOG_INFO("Try to switch to paused state");
        player->pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState != proto::AudioClientState::PAUSED; });
        YIO_LOG_INFO("Not paused");

        YIO_LOG_INFO("Try to switch to resumed state");
        player->resume(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState != proto::AudioClientState::PLAYING; });
        YIO_LOG_INFO("Not playing 3");

        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::FAILED; });
        YIO_LOG_INFO("Still failed");
    }

    Y_UNIT_TEST(lastPlayTimestampCorrectness) {
        proto::AudioClientState lastReceivedState;
        int64_t lastTimestamp = 0;
        int64_t firstPlayingTimestamp = 0;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            lastReceivedState = event.state();
            lastTimestamp = event.last_play_timestamp();
            if (event.event() == proto::AudioClientEvent::STATE_CHANGED && event.state() == proto::AudioClientState::PLAYING && firstPlayingTimestamp == 0) {
                firstPlayingTimestamp = event.last_play_timestamp();
            }
            condVar_.notify_all();
        };

        // play and wait until playing
        const auto streamId = "test_stream_id";
        YIO_LOG_INFO("Trying to play");
        auto player = makePlayer(listener, getDescriptor(streamId), getAudio(streamId));
        player->play(true, false, AudioPlayer::Channel::ALL, {});
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::PLAYING; });
        YIO_LOG_INFO("Started");
        UNIT_ASSERT_GT(firstPlayingTimestamp, 0);
        UNIT_ASSERT_LE(firstPlayingTimestamp, getNowTimestampMs());

        // check, that play timestamp will not change during state switches
        YIO_LOG_INFO("Trying to pause");
        player->pause();
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::PAUSED; });
        YIO_LOG_INFO("Paused");
        UNIT_ASSERT_EQUAL(firstPlayingTimestamp, lastTimestamp);

        // resume playback
        YIO_LOG_INFO("Trying to resume");
        player->resume(true);
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::PLAYING; });
        YIO_LOG_INFO("Resumed");
        UNIT_ASSERT_EQUAL(firstPlayingTimestamp, lastTimestamp);

        // stop playback via destroy
        YIO_LOG_INFO("Trying to stop");
        player.reset();
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::STOPPED; });
        YIO_LOG_INFO("Stopped");
        UNIT_ASSERT_EQUAL(firstPlayingTimestamp, lastTimestamp);
    }

    Y_UNIT_TEST(lastStopTimestamp) {
        proto::AudioClientState lastReceivedState;
        int64_t stopTimestamp = 0;
        int64_t stopTimestampOnPlaying = 0;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            lastReceivedState = event.state();
            stopTimestamp = event.last_stop_timestamp();
            if (event.state() == proto::AudioClientState::PLAYING) {
                stopTimestampOnPlaying = event.last_stop_timestamp();
            }
            condVar_.notify_all();
        };

        // play and wait until playing
        const auto streamId = "test_stream_id";
        YIO_LOG_INFO("Trying to play");
        auto player = makePlayer(listener, getDescriptor(streamId), getAudio(streamId));
        player->play(true, false, AudioPlayer::Channel::ALL, {});
        {
            std::unique_lock lock(stateMutex_);
            condVar_.wait(lock, [&]() { return lastReceivedState == proto::AudioClientState::PLAYING; });
            YIO_LOG_INFO("Started");
            UNIT_ASSERT_EQUAL(stopTimestampOnPlaying, 0); // check that stop timestamp will not be set on PLAYING state
        }

        // wait some time to be sure that timestamps will differ
        int64_t now = getNowTimestampMs();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // stop playback
        YIO_LOG_INFO("Trying to stop via destroy");
        player.reset();
        {
            std::unique_lock lock(stateMutex_);
            condVar_.wait(lock, [&]() { return lastReceivedState == proto::AudioClientState::STOPPED; });
            YIO_LOG_INFO("Stopped");
            UNIT_ASSERT_LT(now, stopTimestamp);
        }
    }

    Y_UNIT_TEST(audioClientEventFieldsGeneration) {
        std::string playingAudioId;
        proto::AudioClientEvent playingEvent;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            if (event.event() == proto::AudioClientEvent::STATE_CHANGED) {
                if (event.state() == proto::AudioClientState::PLAYING) {
                    YIO_LOG_INFO("Event: " << convertMessageToJsonString(event));
                    playingEvent = event;
                    condVar_.notify_all();
                }
            }
        };

        // prepare audio fields to check
        auto audio = proto::Audio();
        audio.set_id("full_stream_id");
        audio.set_url("full_url");
        audio.set_initial_offset_ms(123L);
        audio.set_format(proto::Audio::MP3);
        audio.set_context("full_audio_context");
        audio.mutable_analytics_context()->set_vins_request_id("full_analytics_request_id");
        audio.mutable_analytics_context()->set_name("full_analytics_name");
        audio.mutable_metadata()->set_title("full_metadata_title");
        audio.mutable_metadata()->set_subtitle("full_metadata_subtitle");
        audio.mutable_metadata()->set_art_image_url("full_metadata_art_image_url");
        audio.mutable_metadata()->set_hide_progress_bar(true);
        audio.set_provider_name("full_provider_name");
        audio.set_screen_type(proto::Audio::MUSIC);
        // TODO: cannot check position_sec, duration_sec, played_sec due to fake gstreamer pipeline.
        //  Need investigation to be able to cover playback fields and rewind cases.

        // prepare descriptor fields to check
        proto::AudioPlayerDescriptor descriptor;
        descriptor.set_type(proto::AudioPlayerDescriptor::AUDIO);
        descriptor.set_player_id("full_player_id");
        descriptor.set_stream_id("full_stream_id");

        // play and wait until playing
        YIO_LOG_INFO("Trying to play");
        auto player = makePlayer(listener, descriptor, audio);
        player->play(true, false, AudioPlayer::Channel::ALL, {});

        std::unique_lock lock(stateMutex_);
        condVar_.wait(lock, [&]() {
            return "full_stream_id" == playingEvent.audio().id();
        });
        YIO_LOG_INFO("Started");

        // check generated fields
        UNIT_ASSERT_EQUAL(playingEvent.audio().id(), audio.id());
        // AudioClient appends vsid to url, so remove it from url params
        UNIT_ASSERT_VALUES_EQUAL(removeVsidFromUrl(playingEvent.audio().url()), audio.url());
        UNIT_ASSERT_EQUAL(playingEvent.audio().initial_offset_ms(), audio.initial_offset_ms());
        UNIT_ASSERT_EQUAL(int(playingEvent.audio().format()), int(audio.format()));
        UNIT_ASSERT_EQUAL(playingEvent.audio().context(), audio.context());
        UNIT_ASSERT_EQUAL(playingEvent.audio().analytics_context().vins_request_id(), audio.analytics_context().vins_request_id());
        UNIT_ASSERT_EQUAL(playingEvent.audio().analytics_context().name(), audio.analytics_context().name());
        UNIT_ASSERT_EQUAL(playingEvent.audio().metadata().title(), audio.metadata().title());
        UNIT_ASSERT_EQUAL(playingEvent.audio().metadata().subtitle(), audio.metadata().subtitle());
        UNIT_ASSERT_EQUAL(playingEvent.audio().metadata().art_image_url(), audio.metadata().art_image_url());
        UNIT_ASSERT_EQUAL(playingEvent.audio().metadata().hide_progress_bar(), audio.metadata().hide_progress_bar());
        UNIT_ASSERT_EQUAL(playingEvent.audio().provider_name(), audio.provider_name());
        UNIT_ASSERT_EQUAL(int(playingEvent.audio().screen_type()), int(audio.screen_type()));

        UNIT_ASSERT_EQUAL(int(playingEvent.player_descriptor().type()), int(descriptor.type()));
        UNIT_ASSERT_EQUAL(playingEvent.player_descriptor().player_id(), descriptor.player_id());
        UNIT_ASSERT_EQUAL(playingEvent.player_descriptor().stream_id(), descriptor.stream_id());
    }

    Y_UNIT_TEST(backgroundStateSwitch) {
        proto::AudioClientState lastReceivedState;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            lastReceivedState = event.state();
            condVar_.notify_all();
        };

        // play and wait until playing
        const auto streamId = "test_stream_id";
        YIO_LOG_INFO("Trying to play");
        auto player = makePlayer(listener, getDescriptor(streamId), getAudio(streamId));
        player->play(true, false, AudioPlayer::Channel::ALL, {});
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::PLAYING; });
        YIO_LOG_INFO("Started");

        // check, that after pause player will still be in foreground
        YIO_LOG_INFO("Trying to pause");
        player->pause();
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::PAUSED; });
        YIO_LOG_INFO("Paused");

        // stop playback and check that player is in background
        YIO_LOG_INFO("Trying to stop");
        player.reset();
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::STOPPED; });
        YIO_LOG_INFO("Stopped");

        // play with proper pipeline, but with wrong url - to get failed state
        playbackParams_["MP3"]["gstPipeline"] = GOOD_PIPELINE;
        YIO_LOG_INFO("Trying to play");
        player = makePlayer(listener, getDescriptor(streamId), getAudio(streamId, "some_wrong_url"));
        player->play(true, false, AudioPlayer::Channel::ALL, {});
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::FAILED; });
        YIO_LOG_INFO("Failed");
    }

    Y_UNIT_TEST(updatePlayerDescriptor) {
        proto::AudioClientEvent lastReceivedEvent;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            lastReceivedEvent = event;
            condVar_.notify_all();
        };

        // play with old playerId and wait until playing
        const auto streamId = "test_stream_id";
        const auto oldPlayerId = "old_player_id";
        YIO_LOG_INFO("Trying to play 1");
        auto player = makePlayer(listener, getDescriptor(streamId, oldPlayerId), getAudio(streamId));
        player->play(true, false, AudioPlayer::Channel::ALL, {});
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedEvent.state() == proto::AudioClientState::PLAYING; });
        YIO_LOG_INFO("Started 1");
        UNIT_ASSERT_EQUAL(lastReceivedEvent.player_descriptor().player_id(), oldPlayerId);

        // pause, and check player id again
        YIO_LOG_INFO("Trying to pause");
        player->pause();
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedEvent.state() == proto::AudioClientState::PAUSED; });
        YIO_LOG_INFO("Paused");
        UNIT_ASSERT_EQUAL(lastReceivedEvent.player_descriptor().player_id(), oldPlayerId);

        // update player id, run and check it's update
        YIO_LOG_INFO("Trying to update player context");
        const auto newPlayerId = "new_player_id";
        proto::MediaRequest request;
        request.mutable_player_descriptor()->set_player_id(TString(newPlayerId));
        player->updatePlayerContext(request);
        YIO_LOG_INFO("Trying to play 2");
        player->play(true, false, AudioPlayer::Channel::ALL, {});
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedEvent.state() == proto::AudioClientState::PLAYING; });
        YIO_LOG_INFO("Started 2");
        UNIT_ASSERT_VALUES_EQUAL(lastReceivedEvent.player_descriptor().player_id(), newPlayerId);
    }
}

namespace {

    class SharedControl {
    public:
        void mockFinish() {
            if (onMockFinish_) {
                onMockFinish_();
            }
        }

        void addOnMockFinish(std::function<void()> func) {
            onMockFinish_ = std::move(func);
        }

    private:
        std::function<void()> onMockFinish_;
    };

    class TestAudioPlayer: public AudioPlayer {
    public:
        TestAudioPlayer(const Params& params, std::shared_ptr<SharedControl> control)
            : AudioPlayer(params)
        {
            control->addOnMockFinish([this]() {
                mockFinish();
            });
        }

        ~TestAudioPlayer() = default;

        void mockFinish()
        {
            playing_ = false;
            sendEnd();
        }

        bool playAsync() override {
            playing_ = true;
            sendStart();
            return true;
        };

        bool replayAsync() override {
            seek(0);
            return playAsync();
        };

        bool pause() override {
            playing_ = false;
            sendPaused();
            return true;
        };

        bool seek(int /*ms*/) override {
            sendSeeked();
            return true;
        };

        bool stop() override {
            playing_ = false;
            sendStopped();
            return true;
        };

        bool playMultiroom(std::chrono::nanoseconds /*basetime*/, std::chrono::nanoseconds /*position*/) override {
            return true;
        };

        bool isPlaying() const override {
            return playing_;
        };

        bool startBuffering() override {
            return pause();
        };

        bool setVolume(double /*volume*/) override {
            return true;
        };

        const std::vector<Format>& supportedFormats() const override {
            static std::vector<AudioPlayer::Format> formats{AudioPlayer::Format::FMT_MP3, AudioPlayer::Format::FMT_WAV};
            return formats;
        }

    private:
        std::atomic<bool> playing_{false};
    };

    class TestAudioPlayerFactory: public AudioPlayerFactory {
    public:
        TestAudioPlayerFactory()
            : sharedControl(std::make_shared<SharedControl>())
        {
        }

        ~TestAudioPlayerFactory() = default;

        std::unique_ptr<AudioPlayer> createPlayer(const AudioPlayer::Params& params) override {
            return std::make_unique<TestAudioPlayer>(params, sharedControl);
        }

    public:
        const std::shared_ptr<SharedControl> sharedControl;
    };

} // namespace

class AudioClientFixtureWithTestPlayer: public AudioClientFixture {
public:
    void SetUp(NUnitTest::TTestContext& context) override {
        AudioClientFixture::SetUp(context);

        testAudioPlayerFactory_ = std::make_shared<TestAudioPlayerFactory>();
    }

    void TearDown(NUnitTest::TTestContext& context) override {
        AudioClientFixture::TearDown(context);
    }

    std::shared_ptr<SharedControl> getSharedPlayerControl() const {
        return testAudioPlayerFactory_->sharedControl;
    }

    std::unique_ptr<AudioClient> makePlayer(const std::shared_ptr<TestEventListener>& listener,
                                            const proto::AudioPlayerDescriptor& descriptor,
                                            const proto::Audio& audio) {
        return std::make_unique<AudioClient>(
            getDeviceForTests(),
            *deviceContext_,
            std::shared_ptr<const IAudioClockManager>{},
            testAudioPlayerFactory_,
            listener,
            playbackParams_,
            extraPlaybackParams_,
            audioClientConfig_,
            descriptor,
            audio,
            std::make_shared<gogol::NullGogolSession>(),
            std::make_shared<YandexIO::SpectrumProvider>(ipcFactoryForTests()),
            true);
    }

private:
    std::shared_ptr<TestAudioPlayerFactory> testAudioPlayerFactory_;
};

Y_UNIT_TEST_SUITE_F(audioclientWithTestPlayer, AudioClientFixtureWithTestPlayer) {
    Y_UNIT_TEST(testReplay) {
        proto::AudioClientState lastReceivedState;
        auto listener = std::make_shared<TestEventListener>();
        listener->onEvent = [&](const proto::AudioClientEvent& event) {
            std::lock_guard<std::mutex> guard(stateMutex_);
            lastReceivedState = event.state();
            condVar_.notify_all();
        };

        const auto streamId = "test_stream_id";
        YIO_LOG_INFO("Trying to play");
        auto player = makePlayer(listener, getDescriptor(streamId), getAudio(streamId));
        player->play(true, false, AudioPlayer::Channel::ALL, {});
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::PLAYING; });
        YIO_LOG_INFO("Started");

        YIO_LOG_INFO("Trying to pause");
        player->pause();
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::PAUSED; });
        YIO_LOG_INFO("Paused");

        YIO_LOG_INFO("Trying to resume");
        player->resume(true);
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::PLAYING; });
        YIO_LOG_INFO("Resumed");

        YIO_LOG_INFO("Mock end of stream");
        getSharedPlayerControl()->mockFinish();
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::FINISHED; });
        YIO_LOG_INFO("Finished");

        YIO_LOG_INFO("Trying to replay");
        player->replay(true);
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::PLAYING; });
        YIO_LOG_INFO("Replay started");

        YIO_LOG_INFO("Mock second end of stream");
        getSharedPlayerControl()->mockFinish();
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::FINISHED; });
        YIO_LOG_INFO("Second finish");

        YIO_LOG_INFO("Trying to replay second time");
        player->replay(true);
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::PLAYING; });
        YIO_LOG_INFO("Replay started second time");

        YIO_LOG_INFO("Trying to stop via destroy");
        player.reset();
        waitUntil(condVar_, stateMutex_, [&]() { return lastReceivedState == proto::AudioClientState::STOPPED; });
        YIO_LOG_INFO("Stopped");
    }
}
