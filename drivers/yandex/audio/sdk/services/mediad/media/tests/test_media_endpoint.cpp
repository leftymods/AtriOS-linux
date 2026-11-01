#include <yandex_io/services/mediad/audioclient/audio_client_endpoint.h>
#include <yandex_io/services/mediad/media/media_endpoint.h>
#include <yandex_io/services/mediad/media/players/yandexmusic/yandex_music_player.h>
#include <yandex_io/services/mediad/media/players/yandexradio2/yandex_radio_player2.h>

#include <yandex_io/capabilities/file_player/interfaces/mocks/mock_i_file_player_capability.h>

#include <yandex_io/interfaces/auth/mock/auth_provider.h>
#include <yandex_io/interfaces/device_state/mock/device_state_provider.h>
#include <yandex_io/interfaces/multiroom/mock/multiroom_provider.h>
#include <yandex_io/libs/audio_player/mock/mock_audio_player.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_connector.h>
#include <yandex_io/libs/ipc/i_server.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/protobuf_utils/debug.h>
#include <yandex_io/libs/protobuf_utils/proto_trace.h>
#include <yandex_io/libs/self_destroyer/self_destroyer_utils.h>
#include <yandex_io/protos/quasar_proto.pb.h>
#include <yandex_io/sdk/audio_source/i_audio_source_client.h>
#include <yandex_io/tests/testlib/null_sdk/null_sdk_interface.h>
#include <yandex_io/tests/testlib/mock_ws_server.h>
#include <yandex_io/tests/testlib/test_mediad_utils.h>
#include <yandex_io/tests/testlib/test_utils.h>
#include <yandex_io/tests/testlib/null_device_state_capability/null_device_state_capability.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <json/json.h>

#include <library/cpp/testing/unittest/registar.h>

#include <exception>
#include <future>
#include <memory>

using namespace quasar;
using namespace quasar::TestUtils;
using namespace quasar::proto;

namespace {

    std::optional<google::protobuf::Struct> getStructFromStruct(const google::protobuf::Struct& protoStruct, const TString& field) {
        if (protoStruct.fields().contains(field)) {
            const auto& value = protoStruct.fields().at(field);
            if (value.has_struct_value()) {
                return value.struct_value();
            }
        }
        return std::nullopt;
    }

    std::optional<bool> getBoolFromStruct(const google::protobuf::Struct& protoStruct, const TString& field) {
        if (protoStruct.fields().contains(field)) {
            const auto& value = protoStruct.fields().at(field);
            if (value.has_bool_value()) {
                return value.bool_value();
            }
        }
        return std::nullopt;
    }

    bool getRadioPause(const google::protobuf::Struct& state) {
        if (const auto player = getStructFromStruct(state, "player")) {
            return getBoolFromStruct(*player, "pause").value_or(true);
        }

        return true;
    }

} // unnamed namespace

class MockAudioPlayerFactory: public AudioPlayerFactory {
    std::unique_ptr<AudioPlayer> createPlayer(const AudioPlayer::Params& params) override {
        return std::make_unique<MockAudioPlayer>(params);
    };
};

class MediaFixture: public QuasarUnitTestFixture {
public:
    YandexIO::Configuration::TestGuard testGuard;
    std::unique_ptr<MockWSServer> mockYaMusicServer;
    std::shared_ptr<ipc::IServer> trashd_;
    std::shared_ptr<ipc::IServer> mockBrickd_;
    std::shared_ptr<ipc::IServer> mockAliced_;
    std::shared_ptr<YandexIO::MockIFilePlayerCapability> filePlayerCapabilityMock_;
    const bool initAliceMock_;

    using Base = QuasarUnitTestFixture;

    MediaFixture(bool initAliceMock = true)
        : initAliceMock_{initAliceMock}
    {
    }

    void SetUp(NUnitTest::TTestContext& context) override {
        Base::SetUp(context);

        mockYaMusicServer = std::make_unique<MockWSServer>(getPort());

        auto& config = getDeviceForTests()->configuration()->getMutableConfig(testGuard);
        config["audioclient"]["gogol"]["senderQueueSize"] = 15;

        config["mediad"]["apiUrl"] = "wss://localhost:" + std::to_string(mockYaMusicServer->getPort());
        config["mediad"]["abortOnFreezeTimeout"] = 100; // seconds

        mockYaMusicServer->onMessage = [this](const std::string& msg) {
            Json::Value request = parseJson(msg);
            Json::Value response;
            Json::Value result;
            if (request["action"].asString() == "ping" || request["action"].asString() == "auth") {
                response["reqId"] = request["reqId"].asString();
                result["success"] = true;
                response["result"] = result;
                mockYaMusicServer->send(jsonToString(response));
            } else if (request["action"].asString() == "sync") {
                mockYaMusicServer->send(
                    prepareSyncAddition(request["reqId"].asString(), request["data"]["index"].asInt()));
            } else if (request["action"].asString() == "url") {
                mockYaMusicServer->send(
                    prepareUrl(request["reqId"].asString()));
            } else if (request["action"].asString() == "feedback") {
                std::string feedbackType = request["data"]["type"].asString();
                response["reqId"] = request["reqId"].asString();
                result["success"] = true;
                response["result"] = result;
                mockYaMusicServer->send(jsonToString(response));
            }
        };

        startMockIpcServers({"syncd", "metricad"});

        mockBrickd_ = createIpcServerForTests("brickd");
        mockBrickd_->listenService();

        filePlayerCapabilityMock_ = std::make_shared<YandexIO::MockIFilePlayerCapability>();

        if (initAliceMock_) {
            mockAliced_ = createIpcServerForTests("aliced");
            mockAliced_->listenService();
        }
    }

    void TearDown(NUnitTest::TTestContext& context) override {
        Base::TearDown(context);
    }

    std::shared_ptr<PlayerFactory> createMockPlayerFactory()
    {
        auto factory = std::make_shared<PlayerFactory>(getDeviceForTests(),
                                                       ipcFactoryForTests(),
                                                       nullptr,
                                                       std::make_shared<MockAudioPlayerFactory>(),
                                                       std::make_shared<YandexIO::NullSDKInterface>());
        factory->addYandexMusicConstructor(false, nullptr, nullptr, nullptr);
        factory->addYandexRadioConstructor();
        return factory;
    }
    // used to provide "system_config" values without establishing connection to syncd...
    std::shared_ptr<PlayerFactory> createMockPlayerFactoryWithParams(Json::Value extra) {
        auto factory = std::make_shared<PlayerFactory>(getDeviceForTests(),
                                                       ipcFactoryForTests(),
                                                       nullptr,
                                                       std::make_shared<MockAudioPlayerFactory>(),
                                                       std::make_shared<YandexIO::NullSDKInterface>());
        factory->addYandexRadioConstructor();

        auto baseYandexMusicBuilder = PlayerFactory::buildYandexMusicPlayerConstructor(false, nullptr, nullptr, nullptr);
        auto yandexMusicBuilder = [extra, baseYandexMusicBuilder](
                                      std::shared_ptr<YandexIO::IDevice> device,
                                      std::shared_ptr<ipc::IIpcFactory> ipcFactory,
                                      std::shared_ptr<const IAudioClockManager> clock,
                                      std::shared_ptr<AudioPlayerFactory> factory,
                                      bool ownsFocus,
                                      const Json::Value& customPlayerConfig,
                                      Player::OnStateChange onStateChange,
                                      Player::OnPlayStartChange onPlayStart,
                                      Player::OnError onError) mutable -> std::unique_ptr<Player> {
            jsonMerge(customPlayerConfig, extra);
            return baseYandexMusicBuilder(device, ipcFactory, clock, factory, ownsFocus, extra, onStateChange, onPlayStart, onError);
        };
        factory->addConstructor(PlayerType::YANDEX_MUSIC, yandexMusicBuilder);

        return factory;
    }
};

class MediaAuthFixture: public MediaFixture {
public:
    std::shared_ptr<mock::AuthProvider> mockAuthProvider;
    std::shared_ptr<mock::DeviceStateProvider> mockDeviceStateProvider;
    std::shared_ptr<mock::MultiroomProvider> mockMultiroomProvider;
    std::string currentToken_ = "test_token";

    MediaAuthFixture(bool initAliceMock = true)
        : MediaFixture(initAliceMock)
    {
        mockDeviceStateProvider = std::make_shared<mock::DeviceStateProvider>();
        mockDeviceStateProvider->setDeviceState(mock::defaultDeviceState());

        mockMultiroomProvider = std::make_shared<mock::MultiroomProvider>();

        mockAuthProvider = std::make_shared<mock::AuthProvider>();
        mockAuthProvider->setOwner(
            AuthInfo2{
                .source = AuthInfo2::Source::AUTHD,
                .authToken = currentToken_,
                .passportUid = "Magical Uid",
                .tag = 1600000001,
            });
    }
};

class MediaAuthFixtureWithoutAliceMock: public MediaAuthFixture {
public:
    MediaAuthFixtureWithoutAliceMock()
        : MediaAuthFixture(false)
    {
    }
};

class AudioListener: public AudioPlayer::SimpleListener {
public:
    AudioListener(int id, YandexRadioPlayer2* yandexRadioPlayer) {
        id_ = id;
        yandexRadioPlayer_ = yandexRadioPlayer;
    };

    void onBufferingEnd() override {
        yandexRadioPlayer_->handleBufferingEnd(id_);
    };

private:
    int id_{0};
    YandexRadioPlayer2* yandexRadioPlayer_;
};

class YandexRadioPlayerMocked: public YandexRadioPlayer2 {
public:
    YandexRadioPlayerMocked(std::shared_ptr<YandexIO::IDevice> device,
                            std::shared_ptr<ipc::IIpcFactory> ipcFactory,
                            std::shared_ptr<YandexIO::IFilePlayerCapability> filePlayerCapability)
        : YandexRadioPlayer2(std::move(device), std::move(ipcFactory), std::move(filePlayerCapability), true, std::make_shared<MockAudioPlayerFactory>())
    {
    }

protected:
    std::unique_ptr<AudioPlayer> acqCreatePlayer(int id, const std::string& /*url*/) override {
        std::unique_ptr<AudioPlayer> player(new MockAudioPlayer(AudioPlayer::Params().setURI("file:///dev/null")));
        player->addListener(std::make_shared<AudioListener>(id, this));
        return player;
    };
};

class IOHubMock {
public:
    IOHubMock(std::shared_ptr<ipc::IServer> server, bool startListen = true)
        : server_(std::move(server))
    {
        if (startListen) {
            listen();
        }
    }

    ~IOHubMock() {
        server_->shutdown();
    }

    void listen() {
        server_->listenService();
    }

    void waitUntilConnected(int connections) {
        server_->waitConnectionsAtLeast(connections);
    }

private:
    std::shared_ptr<ipc::IServer> server_;
};

Y_UNIT_TEST_SUITE(mediad) {

    Y_UNIT_TEST_F(testMediadPlayersType, MediaFixture) {
        YandexIO::DeviceContext context(ipcFactoryForTests());

        mockYaMusicServer->onMessage = [&](std::string msg) {
            Json::Value request = parseJson(msg);
            Json::Value response;
            Json::Value result;
            if (request["action"].asString() == "ping") {
                response["reqId"] = request["reqId"].asString();
                result["success"] = true;
                response["result"] = result;
                mockYaMusicServer->send(jsonToString(response));
            }
        };
        auto yandexMusicPlayer = std::make_shared<YandexMusicPlayer>(getDeviceForTests(), ipcFactoryForTests(), false, true,
                                                                     std::make_shared<MockAudioPlayerFactory>(),
                                                                     Json::Value::null);
        auto yandexRadioPlayer = std::make_shared<YandexRadioPlayer2>(getDeviceForTests(), ipcFactoryForTests(), filePlayerCapabilityMock_, true,
                                                                      std::make_shared<MockAudioPlayerFactory>());

        UNIT_ASSERT_VALUES_EQUAL(int(yandexMusicPlayer->type()), int(PlayerType::YANDEX_MUSIC));
        UNIT_ASSERT_VALUES_EQUAL(int(yandexRadioPlayer->type()), int(PlayerType::YANDEX_RADIO));
    }

    Y_UNIT_TEST_F(testYandexMusicPlayerUpdateConfig, MediaFixture) {
        YandexIO::DeviceContext context(ipcFactoryForTests());

        mockYaMusicServer->onMessage = [&](std::string msg) {
            Json::Value request = parseJson(msg);
            Json::Value response;
            Json::Value result;
            if (request["action"].asString() == "ping") {
                response["reqId"] = request["reqId"].asString();
                result["success"] = true;
                response["result"] = result;
                mockYaMusicServer->send(jsonToString(response));
            }
        };

        Json::Value customYandexMusicConfig;
        customYandexMusicConfig["souphttpsrc"]["retries"] = 10;
        {
            auto yandexMusicPlayer = std::make_shared<YandexMusicPlayer>(getDeviceForTests(), ipcFactoryForTests(), false, true, std::make_shared<MockAudioPlayerFactory>(), Json::Value::null);
            auto res = yandexMusicPlayer->updateConfig(customYandexMusicConfig);
            UNIT_ASSERT(res == Player::ChangeConfigResult::CHANGED);
        }

        {
            auto yandexMusicPlayer = std::make_shared<YandexMusicPlayer>(getDeviceForTests(), ipcFactoryForTests(), false, true, std::make_shared<MockAudioPlayerFactory>(), Json::Value::null);
            auto res = yandexMusicPlayer->updateConfig(customYandexMusicConfig);
            UNIT_ASSERT(res == Player::ChangeConfigResult::CHANGED);

            res = yandexMusicPlayer->updateConfig(customYandexMusicConfig);
            UNIT_ASSERT(res == Player::ChangeConfigResult::NO_CHANGES);
        }

        {
            auto yandexMusicPlayer = std::make_shared<YandexMusicPlayer>(getDeviceForTests(), ipcFactoryForTests(), false, true, std::make_shared<MockAudioPlayerFactory>(), Json::Value::null);
            auto res = yandexMusicPlayer->updateConfig(customYandexMusicConfig);
            UNIT_ASSERT(res == Player::ChangeConfigResult::CHANGED);

            res = yandexMusicPlayer->updateConfig(Json::Value::null);
            UNIT_ASSERT(res == Player::ChangeConfigResult::CHANGED);
        }
    }

    Y_UNIT_TEST_F(testYandexRadioPlayerDeadlock, MediaFixture) {
        YandexIO::DeviceContext context(ipcFactoryForTests());

        auto yandexRadioPlayer = std::make_shared<YandexRadioPlayerMocked>(getDeviceForTests(), ipcFactoryForTests(), filePlayerCapabilityMock_);
        yandexRadioPlayer->play(Json::objectValue);
        yandexRadioPlayer->play(Json::objectValue);
    }

    Y_UNIT_TEST_F(testPlayersControllerChangePlayerOnFlight, MediaFixture) {
        /**
         * This test check that when player (radio or music) is changed without prior 'pause' call --> Previous player
         * will be paused by PlayersController and also PlayersController will capture last state of this player and will
         * forward to MediaEndpoint BEFORE starting to play via new player.
         */

        std::mutex jsonMutex;
        google::protobuf::Struct lastRadioState;
        NAlice::TDeviceState::TMusic lastMusicState;
        std::atomic_bool radioPlay1{false};
        std::atomic_bool radioPlay2{false};
        std::atomic_bool musicPlay{false};
        SteadyConditionVariable condVar;

        std::unique_ptr<PlayersController> playersController;
        playersController = std::make_unique<PlayersController>(getDeviceForTests(), createMockPlayerFactory());

        playersController->setOnStateChangeHandler([&](const NAlice::TDeviceState& deviceState) {
            std::lock_guard<std::mutex> guard(jsonMutex);
            if (deviceState.HasRadio()) {
                lastRadioState = deviceState.GetRadio();
                if (getRadioPause(lastRadioState) == false) {
                    /* Radio player will start play twice. When it play first time there won't be any music_state saved.
                     * so check if expected field exist, and if it does then check that Music is paused
                     */
                    if (lastMusicState.HasPlayer() && lastMusicState.GetPlayer().HasPause()) {
                        UNIT_ASSERT(lastMusicState.GetPlayer().GetPause());
                        /* Radio started to play second time. set up bool variable for predicate */
                        radioPlay2 = true;
                    } else {
                        /* Radio started to play first time. Set up bool var for predicate */
                        radioPlay1 = true;
                    }
                }
            } else if (deviceState.HasMusic()) {
                lastMusicState = deviceState.GetMusic();
                if (!lastMusicState.GetPlayer().GetPause()) {
                    UNIT_ASSERT(getRadioPause(lastRadioState));
                    musicPlay = true;
                }
            }
            condVar.notify_all();
        });

        YIO_LOG_INFO("Set up to play Radio");
        Json::Value options;
        options["token"] = "tokEn";
        options["uid"] = makeUUID();
        playersController->play(PlayerType::YANDEX_RADIO, options);

        waitUntil(condVar, [&]() { return playersController->playerType() == PlayerType::YANDEX_RADIO; });
        YIO_LOG_INFO("Set up to play Radio - END");

        waitUntil(condVar, jsonMutex, [&]() { return radioPlay1.load(); });
        YIO_LOG_INFO("Radio play 1 - END");

        YIO_LOG_INFO("Set up to play Music");
        playersController->play(PlayerType::YANDEX_MUSIC, options);
        waitUntil(condVar, [&]() { return playersController->playerType() == PlayerType::YANDEX_MUSIC; });
        YIO_LOG_INFO("Set up to play Music - END");

        waitUntil(condVar, jsonMutex, [&]() { return musicPlay.load(); });
        YIO_LOG_INFO("Music play - END");

        YIO_LOG_INFO("Set up to play Radio again");
        playersController->play(PlayerType::YANDEX_RADIO, options);
        waitUntil(condVar, [&]() { return playersController->playerType() == PlayerType::YANDEX_RADIO; });
        YIO_LOG_INFO("Set up to play Radio again - END");

        waitUntil(condVar, jsonMutex, [&]() { return radioPlay2.load(); });
        YIO_LOG_INFO("Radio play 2 - END");

        std::lock_guard<std::mutex> guard(jsonMutex);
        UNIT_ASSERT(!getRadioPause(lastRadioState));
    }

    Y_UNIT_TEST_F(testSoupHttpUpdateConfig, MediaFixture) {
        YandexIO::DeviceContext context(ipcFactoryForTests());
        auto yandexMusicPlayer = std::make_shared<YandexMusicPlayer>(getDeviceForTests(), ipcFactoryForTests(), false, true, std::make_shared<MockAudioPlayerFactory>(), Json::Value::null);

        // have no access to internal GST pipeline, so check if wrong configs will not be applied
        Json::Value defaultYandexMusicConfig;
        defaultYandexMusicConfig["souphttpsrc"]["retries"] = 3;
        defaultYandexMusicConfig["souphttpsrc"]["timeout"] = 15;

        // apply default values
        auto res = yandexMusicPlayer->updateConfig(defaultYandexMusicConfig);
        UNIT_ASSERT(res == Player::ChangeConfigResult::CHANGED);

        // apply once again
        res = yandexMusicPlayer->updateConfig(defaultYandexMusicConfig);
        UNIT_ASSERT(res == Player::ChangeConfigResult::NO_CHANGES);

        // try to apply unexpected values
        Json::Value customYandexMusicConfig;
        customYandexMusicConfig["souphttpsrc"]["unexpected"] = "some_value";
        res = yandexMusicPlayer->updateConfig(customYandexMusicConfig);
        UNIT_ASSERT(res == Player::ChangeConfigResult::NO_CHANGES);

        // try to apply some wrong values
        customYandexMusicConfig["souphttpsrc"]["retries"] = "wrong_retry_value";
        customYandexMusicConfig["souphttpsrc"]["timeout"] = 15;
        res = yandexMusicPlayer->updateConfig(customYandexMusicConfig);
        UNIT_ASSERT(res == Player::ChangeConfigResult::NO_CHANGES);

        customYandexMusicConfig["souphttpsrc"]["retries"] = 3;
        customYandexMusicConfig["souphttpsrc"]["timeout"] = "wrong_timeout_value";
        res = yandexMusicPlayer->updateConfig(customYandexMusicConfig);
        UNIT_ASSERT(res == Player::ChangeConfigResult::NO_CHANGES);

        // apply correct values
        customYandexMusicConfig["souphttpsrc"]["retries"] = 666;
        customYandexMusicConfig["souphttpsrc"]["timeout"] = 123;
        res = yandexMusicPlayer->updateConfig(customYandexMusicConfig);
        UNIT_ASSERT(res == Player::ChangeConfigResult::CHANGED);
    }

    Y_UNIT_TEST_F(testMediadCrashAfterSyncFail, MediaAuthFixture) {
        auto mediadConnector = createIpcConnectorForTests("mediad");
        mediadConnector->setMessageHandler([&](const auto& message) {
            YIO_LOG_INFO("test received message " << shortUtf8DebugString(*message));
        });

        MediaEndpoint mediaEndpoint(getDeviceForTests(), ipcFactoryForTests(), mockAuthProvider, mockDeviceStateProvider, createMockPlayerFactory(), std::make_shared<YandexIO::NullDeviceStateCapability>(), filePlayerCapabilityMock_);
        mediadConnector->connectToService();
        mediadConnector->waitUntilConnected();

        mockYaMusicServer->onMessage = [&](const std::string& msg) {
            Json::Value request = parseJson(msg);
            Json::Value response;
            Json::Value result;
            if (request["action"].asString() == "ping" || request["action"].asString() == "auth" || request["action"].asString() == "feedback") {
                response["reqId"] = request["reqId"].asString();
                result["success"] = true;
                response["result"] = result;
                mockYaMusicServer->send(jsonToString(response));
            } else if (request["action"].asString() == "sync") {
                // send empty sync response, to emulate backend error
                mockYaMusicServer->send("{\"reqId\":\"" + request["reqId"].asString() + "\",\"data\":{}}");
            } else if (request["action"].asString() == "url") {
                mockYaMusicServer->send(prepareUrl(request["reqId"].asString()));
            }
        };

        QuasarMessage mediaMessage;
        mediaMessage.mutable_media_request()->mutable_play_audio();
        mediaMessage.mutable_media_request()->set_uid("Mock uid");
        mediaMessage.mutable_media_request()->set_session_id("Mock session id");
        for (int i = 0; i < 10; i++) {
            YIO_LOG_INFO("Sending play message: " << i); // there was crash in switchTrack method on second message
            mediadConnector->sendMessage(QuasarMessage{mediaMessage});
        }

        // just to check that some messages will be handled, and process will not crash
        std::this_thread::sleep_for(std::chrono::seconds(1));
        YIO_LOG_INFO("Player handled all messages OK");
    }

    Y_UNIT_TEST_F(testScreenTypeConversion, QuasarUnitTestFixture) {
        UNIT_ASSERT_EQUAL(yandex_io::proto::TVideo_ScreenType_ScreenType_ARRAYSIZE, quasar::proto::AppState_ScreenType_ScreenType_ARRAYSIZE);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_main, (int)quasar::proto::AppState_ScreenType_MAIN);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_main, (int)quasar::proto::AppState_ScreenType_MAIN);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_gallery, (int)quasar::proto::AppState_ScreenType_GALLERY);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_description, (int)quasar::proto::AppState_ScreenType_DESCRIPTION);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_video_player, (int)quasar::proto::AppState_ScreenType_VIDEO_PLAYER);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_payment, (int)quasar::proto::AppState_ScreenType_PAYMENT);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_music_player, (int)quasar::proto::AppState_ScreenType_MUSIC_PLAYER);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_season_gallery, (int)quasar::proto::AppState_ScreenType_SEASON_GALLERY);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_splash, (int)quasar::proto::AppState_ScreenType_SPLASH);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_initialization, (int)quasar::proto::AppState_ScreenType_INITIALIZATION);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_bluetooth, (int)quasar::proto::AppState_ScreenType_BLUETOOTH);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_brick, (int)quasar::proto::AppState_ScreenType_BRICK);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_tv_gallery, (int)quasar::proto::AppState_ScreenType_TV_GALLERY);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_radio_player, (int)quasar::proto::AppState_ScreenType_RADIO_PLAYER);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_other, (int)quasar::proto::AppState_ScreenType_OTHER);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_mordovia_webview, (int)quasar::proto::AppState_ScreenType_MORDOVIA_WEBVIEW);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_call_screen, (int)quasar::proto::AppState_ScreenType_CALL_SCREEN);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_rcu_settings, (int)quasar::proto::AppState_ScreenType_RCU_SETTINGS);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_search_results, (int)quasar::proto::AppState_ScreenType_SEARCH_RESULTS);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_tv_main, (int)quasar::proto::AppState_ScreenType_TV_MAIN);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_content_details, (int)quasar::proto::AppState_ScreenType_CONTENT_DETAILS);
        UNIT_ASSERT_EQUAL((int)yandex_io::proto::TVideo_ScreenType_tv_expanded_collection, (int)quasar::proto::AppState_ScreenType_TV_EXPANDED_COLLECTION);
        // Add here new screens here
    }
}
