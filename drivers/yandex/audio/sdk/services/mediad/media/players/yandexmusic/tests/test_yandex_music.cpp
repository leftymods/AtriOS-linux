#include <yandex_io/services/mediad/media/players/yandexmusic/request_timeout_exception.h>
#include <yandex_io/services/mediad/media/players/yandexmusic/yandex_music.h>

#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_server.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/tests/testlib/mock_ws_server.h>
#include <yandex_io/tests/testlib/test_mediad_utils.h>
#include <yandex_io/tests/testlib/test_utils.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <json/json.h>

#include <library/cpp/testing/unittest/registar.h>

#include <exception>
#include <future>
#include <memory>

using namespace quasar;
using namespace quasar::TestUtils;

class Fixture: public QuasarUnitTestFixture {
public:
    YandexIO::Configuration::TestGuard testGuard;
    std::unique_ptr<MockWSServer> mockYaMusicServer;

    using Base = QuasarUnitTestFixture;

    void SetUp(NUnitTest::TTestContext& context) override {
        Base::SetUp(context);

        mockYaMusicServer = std::make_unique<MockWSServer>(getPort());

        auto& config = getDeviceForTests()->configuration()->getMutableConfig(testGuard);
        config["mediad"]["apiUrl"] = "wss://localhost:" + std::to_string(mockYaMusicServer->getPort());

        startMockIpcServers({"syncd", "iohub_services", "metricad"});
    }

    void TearDown(NUnitTest::TTestContext& context) override {
        Base::TearDown(context);
    }

protected:
    static Json::Value getYandexMusicConfigForRetries() {
        Json::Value customYandexMusicConfig;
        // use small request timeouts to trigger retries
        customYandexMusicConfig["requestTimeoutMs"] = 500;
        return customYandexMusicConfig;
    }

    static Json::Value getDefaultYandexMusicConfig() {
        Json::Value customYandexMusicConfig;
        // set request timeout more than SMALL test size, so test client should wait for answer forever
        customYandexMusicConfig["totalRequestTimeoutMs"] = 100 * 1000;
        customYandexMusicConfig["requestTimeoutMs"] = 100 * 1000;
        return customYandexMusicConfig;
    }
};

Y_UNIT_TEST_SUITE_F(yandexmusic, Fixture) {
    Y_UNIT_TEST(testYandexMusicConnection) {
        std::atomic_int pingCount{0};
        std::promise<bool> pingPromise;
        std::mutex mtx;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            std::lock_guard<std::mutex> lock(mtx);
            Json::Value request = parseJson(msg);
            if (request["action"].asString() == "ping") {
                mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
                pingCount++;
                if (pingCount == 2) {
                    pingPromise.set_value(true);
                }
            }
        };

        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
        YandexMusic yandexMusic(getDeviceForTests(), params, getDefaultYandexMusicConfig(), []() {});
        mockYaMusicServer->waitUntilConnections(1, 0);

        UNIT_ASSERT_VALUES_EQUAL(pingPromise.get_future().get(), true);
    }

    Y_UNIT_TEST(testYandexMusicUpdateConfig) {
        Json::Value defaultConfig;
        defaultConfig["connectTimeoutMs"] = 100; // Empty config, but it has small connect timeout to make destructor faster

        Json::Value customYandexMusicConfig = getDefaultYandexMusicConfig();
        customYandexMusicConfig["apiUrl"] = "wss://ws-api.music.yandex.net/quasar/websocket";
        customYandexMusicConfig["connectTimeoutMs"] = 100;

        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::No};
        {
            YandexMusic yandexMusic(getDeviceForTests(), params, defaultConfig, []() {});
            auto res = yandexMusic.updateConfig(defaultConfig);
            UNIT_ASSERT_VALUES_EQUAL(res == Player::ChangeConfigResult::NO_CHANGES, true);
        }

        {
            YandexMusic yandexMusic(getDeviceForTests(), params, customYandexMusicConfig, []() {});
            auto res = yandexMusic.updateConfig(customYandexMusicConfig);
            UNIT_ASSERT_VALUES_EQUAL(res == Player::ChangeConfigResult::NO_CHANGES, true);
        }

        {
            YandexMusic yandexMusic(getDeviceForTests(), params, customYandexMusicConfig, []() {});
            auto res = yandexMusic.updateConfig(defaultConfig);
            UNIT_ASSERT_VALUES_EQUAL(res == Player::ChangeConfigResult::NEED_RECREATE, true);
        }

        {
            YandexMusic yandexMusic(getDeviceForTests(), params, defaultConfig, []() {});
            auto res = yandexMusic.updateConfig(customYandexMusicConfig);
            UNIT_ASSERT_VALUES_EQUAL(res == Player::ChangeConfigResult::NEED_RECREATE, true);
        }

        {
            Json::Value newYandexMusicConfig;
            newYandexMusicConfig["apiUrl"] = "wss://ws-api.music.yandex.net/quasar/websocket1";
            newYandexMusicConfig["connectTimeoutMs"] = 100;
            YandexMusic yandexMusic(getDeviceForTests(), params, customYandexMusicConfig, []() {});
            auto res = yandexMusic.updateConfig(newYandexMusicConfig);
            UNIT_ASSERT_VALUES_EQUAL(res == Player::ChangeConfigResult::NEED_RECREATE, true);
        }

        {
            Json::Value newYandexMusicConfig;
            newYandexMusicConfig["apiUrl"] = customYandexMusicConfig["apiUrl"];
            newYandexMusicConfig["totalRequestTimeoutMs"] = 42;
            newYandexMusicConfig["connectTimeoutMs"] = 100;
            YandexMusic yandexMusic(getDeviceForTests(), params, customYandexMusicConfig, []() {});
            auto res = yandexMusic.updateConfig(newYandexMusicConfig);
            UNIT_ASSERT_VALUES_EQUAL(res == Player::ChangeConfigResult::CHANGED, true);
        }
    }

    Y_UNIT_TEST(testYandexMusicWithRetries) {
        std::promise<bool> pingPromise;
        std::mutex mtx;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            std::lock_guard<std::mutex> lock(mtx);
            Json::Value request = parseJson(msg);
            if (request["action"].asString() == "ping" && request["retry"].asInt() == 2) {
                mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
                pingPromise.set_value(true);
            }
        };

        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
        YandexMusic yandexMusic(getDeviceForTests(), params, getYandexMusicConfigForRetries(), []() {});
        mockYaMusicServer->waitUntilConnections(1, 0);

        UNIT_ASSERT_VALUES_EQUAL(pingPromise.get_future().get(), true);
    }

    Y_UNIT_TEST(testYandexMusicReconnection) {
        std::atomic_int pingCount{0};
        std::promise<bool> pingPromise;
        std::mutex mtx;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            std::lock_guard<std::mutex> lock(mtx);
            Json::Value request = parseJson(msg);
            if (request["action"].asString() == "ping") {
                pingCount++;
                if (pingCount == YandexMusic::RETRIES_COUNT + 1) {
                    mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
                    pingPromise.set_value(true);
                }
            }
        };

        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
        YandexMusic yandexMusic(getDeviceForTests(), params, getYandexMusicConfigForRetries(), []() {});
        // wait for connect
        mockYaMusicServer->waitUntilConnections(1, 0);
        // wait for disconnect
        mockYaMusicServer->waitUntilConnections(1, 1);
        // wait for reconnect
        mockYaMusicServer->waitUntilConnections(2, 1);

        UNIT_ASSERT_VALUES_EQUAL(pingPromise.get_future().get(), true);
    }

    Y_UNIT_TEST(testYandexMusicGenerateRetryId) {
        std::atomic_int pingCount{0};
        std::mutex mtx;
        std::string lastRequestId;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            std::lock_guard<std::mutex> lock(mtx);
            YIO_LOG_INFO("Message received: " << msg);
            Json::Value request = parseJson(msg);
            if (request["action"].asString() == "ping") {
                pingCount = request["retry"].asInt();

                // should generate new request id for every retry
                UNIT_ASSERT_VALUES_UNEQUAL(lastRequestId, request["reqId"].asString());
                lastRequestId = request["reqId"].asString();
            }
        };

        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
        YandexMusic yandexMusic(getDeviceForTests(), params, getYandexMusicConfigForRetries(), []() {});
        // wait for connect
        mockYaMusicServer->waitUntilConnections(1, 0);
        // wait for disconnect
        mockYaMusicServer->waitUntilConnections(1, 1);
        // wait for reconnect
        mockYaMusicServer->waitUntilConnections(2, 1);
    }

    Y_UNIT_TEST(testYandexMusicSameRetryId) {
        std::atomic_int pingCount{0};
        std::mutex mtx;
        std::string firstRequestId;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            std::lock_guard<std::mutex> lock(mtx);
            YIO_LOG_INFO("Message received: " << msg);
            Json::Value request = parseJson(msg);
            if (request["action"].asString() == "ping") {
                pingCount = request["retry"].asInt();
                if (pingCount == 0) {
                    firstRequestId = request["reqId"].asString();
                } else {
                    // should NOT generate new request id for every retry
                    UNIT_ASSERT_VALUES_EQUAL(firstRequestId, request["reqId"].asString());
                }
            }
        };

        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
        Json::Value config = getYandexMusicConfigForRetries();
        config["generateRetryId"] = false;
        YandexMusic yandexMusic(getDeviceForTests(), params, config, []() {});
        // wait for connect
        mockYaMusicServer->waitUntilConnections(1, 0);
        // wait for disconnect
        mockYaMusicServer->waitUntilConnections(1, 1);
        // wait for reconnect
        mockYaMusicServer->waitUntilConnections(2, 1);
    }

    Y_UNIT_TEST(testYandexMusicSync) {
        std::atomic_int urlRequestCount{0};
        std::mutex mtx;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            std::lock_guard<std::mutex> lock(mtx);
            Json::Value request = parseJson(msg);
            if (request["action"].asString() == "ping" || request["action"].asString() == "auth" || request["action"].asString() == "feedback") {
                mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
            } else if (request["action"].asString() == "sync") {
                mockYaMusicServer->send(prepareSyncAddition(request["reqId"].asString(), request["data"]["index"].asInt()));
            } else if (request["action"].asString() == "url") {
                mockYaMusicServer->send(prepareUrl(request["reqId"].asString(), std::to_string(urlRequestCount++)));
            }
        };

        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
        YandexMusic yandexMusic(getDeviceForTests(), params, getDefaultYandexMusicConfig(), []() {});
        // wait for connect
        mockYaMusicServer->waitUntilConnections(1, 0);
        yandexMusic.setAuthData("123", "test_session_id", "123123", "device_id");
        auto track = yandexMusic.start();
        UNIT_ASSERT_VALUES_EQUAL(track->id, "37193494");
        UNIT_ASSERT_VALUES_EQUAL(getPrefixFromUrl(track->url), "0");

        track = yandexMusic.next();
        UNIT_ASSERT_VALUES_EQUAL(track->id, "6119803");
        UNIT_ASSERT_VALUES_EQUAL(getPrefixFromUrl(track->url), "1");

        track = yandexMusic.prev();
        UNIT_ASSERT_VALUES_EQUAL(track->id, "37193494");
        UNIT_ASSERT_VALUES_EQUAL(getPrefixFromUrl(track->url), "2");

        track = yandexMusic.next();
        UNIT_ASSERT_VALUES_EQUAL(track->id, "6119803");
        UNIT_ASSERT_VALUES_EQUAL(getPrefixFromUrl(track->url), "3");

        track = yandexMusic.next();
        UNIT_ASSERT_VALUES_EQUAL(track->id, "755501");
        UNIT_ASSERT_VALUES_EQUAL(getPrefixFromUrl(track->url), "4");
    }

    Y_UNIT_TEST(testYandexMusicSyncWithShots) {
        std::atomic_int urlRequestCount{0};
        std::mutex mtx;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            std::lock_guard<std::mutex> lock(mtx);
            Json::Value request = parseJson(msg);
            if (request["action"].asString() == "ping" || request["action"].asString() == "auth" || request["action"].asString() == "feedback") {
                mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
            } else if (request["action"].asString() == "sync") {
                mockYaMusicServer->send(prepareSyncAdditionWithShots(request["reqId"].asString(), request["data"]["index"].asInt()));
            } else if (request["action"].asString() == "url") {
                mockYaMusicServer->send(prepareUrl(request["reqId"].asString(), std::to_string(urlRequestCount++)));
            }
        };

        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
        YandexMusic yandexMusic(getDeviceForTests(), params, getDefaultYandexMusicConfig(), []() {});
        // wait for connect
        mockYaMusicServer->waitUntilConnections(1, 0);
        yandexMusic.setAuthData("123", "test_session_id", "123123", "test_device_id");
        auto track = yandexMusic.start();
        UNIT_ASSERT_VALUES_EQUAL(track->id, "37193494");
        UNIT_ASSERT_VALUES_EQUAL(getPrefixFromUrl(track->url), "0");

        track = yandexMusic.next();
        UNIT_ASSERT_VALUES_EQUAL(track->id, "shot2");
        UNIT_ASSERT_VALUES_EQUAL(track->url, "example.com/test.mp3");

        track = yandexMusic.next();
        UNIT_ASSERT_VALUES_EQUAL(track->id, "6119803");
        UNIT_ASSERT_VALUES_EQUAL(getPrefixFromUrl(track->url), "1");

        track = yandexMusic.prev();
        UNIT_ASSERT_VALUES_EQUAL(track->id, "shot1");
        UNIT_ASSERT_VALUES_EQUAL(track->url, "example.com/test.mp3");

        track = yandexMusic.next();
        UNIT_ASSERT_VALUES_EQUAL(track->id, "37193494");
        UNIT_ASSERT_VALUES_EQUAL(getPrefixFromUrl(track->url), "2");

        track = yandexMusic.next();
        UNIT_ASSERT_VALUES_EQUAL(track->id, "shot2");
        UNIT_ASSERT_VALUES_EQUAL(track->url, "example.com/test.mp3");

        track = yandexMusic.next();
        UNIT_ASSERT_VALUES_EQUAL(track->id, "6119803");
        UNIT_ASSERT_VALUES_EQUAL(getPrefixFromUrl(track->url), "3");

        track = yandexMusic.next();
        UNIT_ASSERT_VALUES_EQUAL(track->id, "shot3");
        UNIT_ASSERT_VALUES_EQUAL(track->url, "example.com/test.mp3");

        track = yandexMusic.next();
        UNIT_ASSERT_VALUES_EQUAL(track->id, "755501");
        UNIT_ASSERT_VALUES_EQUAL(getPrefixFromUrl(track->url), "4");
    }

    Y_UNIT_TEST(testYandexMusicReauth) {
        std::atomic_int authCount{0};
        SteadyConditionVariable condVar;
        std::mutex mtx;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            std::lock_guard<std::mutex> lock(mtx);
            Json::Value request = parseJson(msg);
            if (request["action"].asString() == "ping" || request["action"].asString() == "auth" || request["action"].asString() == "feedback") {
                if (request["action"].asString() == "auth") {
                    authCount++;
                }
                mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
            } else if (request["action"].asString() == "sync") {
                mockYaMusicServer->send(prepareSyncAddition(request["reqId"].asString(), request["data"]["index"].asInt()));
            } else if (request["action"].asString() == "url") {
                mockYaMusicServer->send(prepareUrl(request["reqId"].asString()));
            }
            condVar.notify_all();
        };

        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
        YandexMusic yandexMusic(getDeviceForTests(), params, getDefaultYandexMusicConfig(), []() {});
        // wait for connect
        mockYaMusicServer->waitUntilConnections(1, 0);
        yandexMusic.setAuthData("", "test_session_id", "111111", "test_device_id");
        yandexMusic.start();
        // token changed
        yandexMusic.setAuthData("", "test_session_id", "changed_token", "test_device_id");
        yandexMusic.start();
        // uid changed
        yandexMusic.setAuthData("changed_uid", "test_session_id", "111111", "test_device_id");
        yandexMusic.start();

        waitUntil(condVar, mtx, [&]() { return authCount == 3; });
        UNIT_ASSERT_VALUES_EQUAL(authCount.load(), 3);
    }

    Y_UNIT_TEST(testYandexMusicPlayerNextAfterStart) {
        std::atomic_int urlCount{0};
        SteadyConditionVariable condVar;
        std::mutex mtx;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            std::lock_guard<std::mutex> lock(mtx);
            Json::Value request = parseJson(msg);
            if (request["action"].asString() == "ping" || request["action"].asString() == "auth" || request["action"].asString() == "feedback") {
                mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
            } else if (request["action"].asString() == "sync") {
                mockYaMusicServer->send(prepareSyncAddition(request["reqId"].asString(), request["data"]["index"].asInt()));
            } else if (request["action"].asString() == "url") {
                mockYaMusicServer->send(prepareUrl(request["reqId"].asString()));
                urlCount++;
            }
            condVar.notify_all();
        };

        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
        YandexMusic yandexMusic(getDeviceForTests(), params, getDefaultYandexMusicConfig(), []() {});
        YIO_LOG_INFO("wait for connect");
        mockYaMusicServer->waitUntilConnections(1, 0);
        YIO_LOG_INFO("wait for connect DONE");
        yandexMusic.setAuthData("test_uid", "test_session_id", "test_token", "test_device_id");
        YIO_LOG_INFO("auth data set");

        auto track = yandexMusic.start();
        waitUntil(condVar, mtx, [&]() { return urlCount == 1; });
        UNIT_ASSERT_VALUES_EQUAL(track->id, "37193494");

        track = yandexMusic.next();
        waitUntil(condVar, mtx, [&]() { return urlCount == 2; });
        UNIT_ASSERT_VALUES_EQUAL(track->id, "6119803");
    }

    Y_UNIT_TEST(testYandexMusicPlayerFeedback) {
        std::atomic_int startFbCount{0};
        std::atomic_int endFbCount{0};
        std::atomic_int skipFbCount{0};
        std::atomic_int prevFbCount{0};

        SteadyConditionVariable condVar;
        std::mutex mtx;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            std::lock_guard<std::mutex> lock(mtx);
            Json::Value request = parseJson(msg);
            if (request["action"].asString() == "ping" || request["action"].asString() == "auth") {
                mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
            } else if (request["action"].asString() == "sync") {
                mockYaMusicServer->send(prepareSyncAddition(request["reqId"].asString(), request["data"]["index"].asInt()));
            } else if (request["action"].asString() == "url") {
                mockYaMusicServer->send(prepareUrl(request["reqId"].asString()));
            } else if (request["action"].asString() == "feedback") {
                std::string feedbackType = request["data"]["type"].asString();
                if (request["retry"].asInt() == 0) {
                    if (feedbackType == "start") {
                        startFbCount++;
                    } else if (feedbackType == "end") {
                        endFbCount++;
                    } else if (feedbackType == "skip") {
                        skipFbCount++;
                    } else if (feedbackType == "prev") {
                        prevFbCount++;
                    }
                }
                mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
            }
            condVar.notify_all();
        };

        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
        YandexMusic yandexMusic(getDeviceForTests(), params, getDefaultYandexMusicConfig(), []() {});
        YIO_LOG_INFO("wait for connect");
        mockYaMusicServer->waitUntilConnections(1, 0);
        YIO_LOG_INFO("wait for connect DONE");
        yandexMusic.setAuthData("test_uid", "test_session_id", "test_token", "test_device_id");
        YIO_LOG_INFO("auth data set");

        yandexMusic.start();
        yandexMusic.next(true);
        yandexMusic.next(false);
        yandexMusic.next(false);
        yandexMusic.prev();

        // start command == 5 should be last
        waitUntil(condVar, mtx, [&]() { return startFbCount == 5; });

        UNIT_ASSERT_VALUES_EQUAL(startFbCount.load(), 5);
        UNIT_ASSERT_VALUES_EQUAL(endFbCount.load(), 2);
        UNIT_ASSERT_VALUES_EQUAL(skipFbCount.load(), 1);
        UNIT_ASSERT_VALUES_EQUAL(prevFbCount.load(), 1);
    }

    Y_UNIT_TEST(testYandexMusicAfterServerDisconnect) {
        std::atomic_int syncCount{0};
        std::atomic_int authCount{0};
        std::promise<bool> syncPromise;
        std::mutex mtx;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            std::lock_guard<std::mutex> lock(mtx);
            Json::Value request = parseJson(msg);
            if (request["action"].asString() == "ping" || request["action"].asString() == "auth" || request["action"].asString() == "feedback") {
                mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
            } else if (request["action"].asString() == "sync") {
                syncCount++;
                YIO_LOG_INFO(syncCount);
                // will close first connection
                if (syncCount == 1) {
                    mockYaMusicServer->closeConnections();
                }
                if (syncCount == 2) {
                    if (request["retry"].asInt() == 0) {
                        auto exception = std::runtime_error("not retry");
                        syncPromise.set_exception(make_exception_ptr(exception));
                    } else {
                        mockYaMusicServer->send(prepareSyncAddition(request["reqId"].asString(), request["data"]["index"].asInt()));
                        syncPromise.set_value(true);
                    }
                }
            } else if (request["action"].asString() == "url") {
                mockYaMusicServer->send(
                    prepareUrl(request["reqId"].asString()));
            }

            if (request["action"].asString() == "auth" && request["retry"].asInt() == 0) { // count only first auth try
                ++authCount;
            }
        };

        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
        YandexMusic yandexMusic(getDeviceForTests(), params, getYandexMusicConfigForRetries(), []() {});
        YIO_LOG_INFO("wait for connect");
        mockYaMusicServer->waitUntilConnections(1, 0);
        YIO_LOG_INFO("wait for connect DONE");
        yandexMusic.setAuthData("test_uid", "test_session_id", "test_token", "test_device_id");
        YIO_LOG_INFO("auth data set");
        yandexMusic.start();
        YIO_LOG_INFO("wait for disconnect");
        mockYaMusicServer->waitUntilConnections(1, 1);
        YIO_LOG_INFO("wait for reconnect");
        mockYaMusicServer->waitUntilConnections(2, 1);

        YIO_LOG_INFO("wait for successfull sync and auth after reconnect");
        UNIT_ASSERT_VALUES_EQUAL(syncPromise.get_future().get(), true);
        UNIT_ASSERT_VALUES_EQUAL(authCount.load(), 2);
    }

    Y_UNIT_TEST(testYandexMusicTimeoutOnConnection) {
        auto config = getDefaultYandexMusicConfig();
        config["apiUrl"] = "wss://NotReachableSite.NotExistingDomain";
        config["totalRequestTimeoutMs"] = 200;
        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
        YandexMusic yandexMusic(getDeviceForTests(), params, config, []() {});
        bool errorThrown = false;

        try {
            yandexMusic.start();
        } catch (const RequestTimeoutException& e) {
            YIO_LOG_INFO("Error thrown in test: " << e.what());
            errorThrown = true;
        }
        UNIT_ASSERT(errorThrown);
    }

    Y_UNIT_TEST(testYandexMusicTimeoutOnAuth) {
        std::mutex mtx;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            std::lock_guard<std::mutex> lock(mtx);
            Json::Value request = parseJson(msg);
            if (request["action"].asString() != "auth") {
                mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
            }
        };

        auto config = getDefaultYandexMusicConfig();
        config["totalRequestTimeoutMs"] = 500;

        bool errorThrown = false;
        {
            YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
            YandexMusic yandexMusic(getDeviceForTests(), params, config, []() {});

            YIO_LOG_INFO("wait for connect");
            mockYaMusicServer->waitUntilConnections(1, 0);
            YIO_LOG_INFO("wait for connect DONE");
            yandexMusic.setAuthData("test_uid", "test_session_id", "test_token", "test_device_id");
            YIO_LOG_INFO("auth data set");

            try {
                yandexMusic.start();
            } catch (const RequestTimeoutException& e) {
                YIO_LOG_INFO("Error thrown in test: " << e.what());
                errorThrown = true;
            }
        }

        YIO_LOG_INFO("wait for disconnect");
        mockYaMusicServer->waitUntilConnections(1, 1);
        UNIT_ASSERT(errorThrown);
    }

    Y_UNIT_TEST(testYandexMusicTimeoutOnRequest) {
        std::mutex mtx;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            std::lock_guard<std::mutex> lock(mtx);
            Json::Value request = parseJson(msg);
            if (request["action"].asString() != "sync") {
                mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
            }
        };

        auto config = getDefaultYandexMusicConfig();
        config["totalRequestTimeoutMs"] = 500;

        bool errorThrown = false;
        {
            YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
            YandexMusic yandexMusic(getDeviceForTests(), params, config, []() {});

            YIO_LOG_INFO("wait for connect");
            mockYaMusicServer->waitUntilConnections(1, 0);
            YIO_LOG_INFO("wait for connect DONE");
            yandexMusic.setAuthData("test_uid", "test_session_id", "test_token", "test_device_id");
            YIO_LOG_INFO("auth data set");

            try {
                yandexMusic.start();
            } catch (const RequestTimeoutException& e) {
                YIO_LOG_INFO("Error thrown in test: " << e.what());
                errorThrown = true;
            }
        }

        YIO_LOG_INFO("wait for disconnect");
        mockYaMusicServer->waitUntilConnections(1, 1);
        UNIT_ASSERT(errorThrown);
    }

    Y_UNIT_TEST(testYandexMusicTimeoutIfNoAuthData) {
        std::atomic_int authCount{0};

        std::mutex mtx;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            std::lock_guard<std::mutex> lock(mtx);
            Json::Value request = parseJson(msg);
            if (request["action"].asString() == "auth") {
                authCount++; // should not be incremented in this test
            }
            mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
        };

        auto config = getDefaultYandexMusicConfig();
        config["totalRequestTimeoutMs"] = 500;

        bool errorThrown = false;
        {
            YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
            YandexMusic yandexMusic(getDeviceForTests(), params, config, []() {});

            YIO_LOG_INFO("wait for connect");
            mockYaMusicServer->waitUntilConnections(1, 0);
            YIO_LOG_INFO("wait for connect DONE");

            try {
                yandexMusic.start();
            } catch (const RequestTimeoutException& e) {
                YIO_LOG_INFO("Error thrown in test: " << e.what());
                errorThrown = true;
            }
        }

        YIO_LOG_INFO("wait for disconnect");
        mockYaMusicServer->waitUntilConnections(1, 1);

        UNIT_ASSERT(errorThrown);
        UNIT_ASSERT_VALUES_EQUAL(authCount.load(), 0);
    }

    Y_UNIT_TEST(testYandexMusicAuthBackoff) {
        for (bool canRetry : {false, true}) {
            std::vector<std::chrono::steady_clock::time_point> authPoints;

            std::mutex mtx;
            SteadyConditionVariable condVar;
            mockYaMusicServer->onMessage = [&](std::string msg) {
                std::lock_guard<std::mutex> lock(mtx);
                Json::Value request = parseJson(msg);
                if (request["action"].asString() == "auth") {
                    authPoints.push_back(std::chrono::steady_clock::now());
                }
                Json::Value response;
                Json::Value result;
                response["reqId"] = request["reqId"].asString();
                result["success"] = false;
                result["canRetry"] = canRetry;
                response["result"] = result;
                mockYaMusicServer->send(jsonToString(response));
                condVar.notify_all();
            };

            auto config = getDefaultYandexMusicConfig();
            config["totalRequestTimeoutMs"] = 200;
            config["connectionRetry"]["firstDelayMs"] = 100;
            config["connectionRetry"]["maxDelayMs"] = 400;
            config["connectionRetry"]["factor"] = 2;

            std::vector<long> expectedDelays = {100, 200, 400, 400};

            {
                YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
                YandexMusic yandexMusic(getDeviceForTests(), params, config, []() {});
                yandexMusic.setAuthData("test_uid", "test_session_id", "test_token", "test_device_id");

                mockYaMusicServer->waitUntilConnections(1, 0);
                waitUntil(condVar, mtx, [&]() { return authPoints.size() > expectedDelays.size(); });
            }

            mockYaMusicServer->waitUntilConnections(1, 1);

            UNIT_ASSERT(authPoints.size() > expectedDelays.size());

            for (size_t i = 0; i < expectedDelays.size(); ++i) {
                auto& t0 = authPoints[i];
                auto& t1 = authPoints[i + 1];
                auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

                std::stringstream assertMessage;
                assertMessage << "canRetry = " << canRetry
                              << ", delay = " << delay
                              << ", i = " << i
                              << ", expectedDelays[i] = " << expectedDelays[i];

                UNIT_ASSERT_C(expectedDelays[i] <= delay, assertMessage.str());
            }
        }
    }

    Y_UNIT_TEST(testYandexMusicAuthFailure) {
        std::promise<bool> authRequestPromise;
        mockYaMusicServer->onMessage = [&](std::string msg) {
            Json::Value request = parseJson(msg);
            Json::Value response;
            response["reqId"] = request["reqId"].asString();

            const auto action = request["action"].asString();
            if (action == "auth") {
                response["result"]["success"] = ("good_token" == request["data"]["owner"].asString());
                response["result"]["canRetry"] = false;
            } else {
                response["result"]["success"] = true;
            }

            mockYaMusicServer->send(jsonToString(response));

            if (action == "auth") {
                authRequestPromise.set_value(response["result"]["success"].asBool());
            }
        };

        const auto onAuthErrorHandler = []() {
            YIO_LOG_INFO("onAuthErrorHandler is called")
            static bool called = false;
            called = !called;
            // We want this callback to be called only one time.
            UNIT_ASSERT(called);
        };

        {
            YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::Yes};
            YandexMusic yandexMusic(getDeviceForTests(), params, getDefaultYandexMusicConfig(), onAuthErrorHandler);
            yandexMusic.setAuthData("test_uid", "test_session_id", "bad_token", "test_device_id");
            mockYaMusicServer->waitUntilConnections(1, 0);

            UNIT_ASSERT(!authRequestPromise.get_future().get());

            authRequestPromise = std::promise<bool>{};

            yandexMusic.setAuthData("test_uid", "test_session_id", "good_token", "test_device_id");

            UNIT_ASSERT(authRequestPromise.get_future().get());
        }

        mockYaMusicServer->waitUntilConnections(1, 1);
    }

    Y_UNIT_TEST(testLowBitrate) {
        auto lowBitratePromise = std::make_shared<std::promise<void>>();
        auto defaultBitratePromise = std::make_shared<std::promise<void>>();
        mockYaMusicServer->onMessage = [lowBitratePromise, defaultBitratePromise, this](std::string msg) {
            Json::Value request = parseJson(msg);
            if (request["action"].asString() == "ping" || request["action"].asString() == "auth" || request["action"].asString() == "feedback") {
                mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
            } else if (request["action"].asString() == "sync") {
                mockYaMusicServer->send(prepareSyncAddition(request["reqId"].asString(), request["data"]["index"].asInt()));
            } else if (request["action"].asString() == "url") {
                mockYaMusicServer->send(prepareUrl(request["reqId"].asString()));
                // actual test logic: check that 1 of request will be with true "lowBitrate" flag
                UNIT_ASSERT(request["data"].isMember("lowBitrate"));
                if (request["data"]["lowBitrate"].asBool()) {
                    UNIT_ASSERT_NO_EXCEPTION(lowBitratePromise->set_value());
                } else {
                    UNIT_ASSERT_NO_EXCEPTION(defaultBitratePromise->set_value());
                }
            }
        };

        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::No};
        YandexMusic yandexMusic(getDeviceForTests(), params, getDefaultYandexMusicConfig(), []() {});
        YIO_LOG_INFO("wait for connect");
        mockYaMusicServer->waitUntilConnections(1, 0);
        YIO_LOG_INFO("wait for connect DONE");
        yandexMusic.setAuthData("test_uid", "test_session_id", "test_token", "test_device_id");
        YIO_LOG_INFO("auth data set");

        auto track = yandexMusic.next();
        YIO_LOG_INFO("Got request with default bitrate");
        defaultBitratePromise->get_future().get();

        auto lowBitrateConfig = getDefaultYandexMusicConfig();
        lowBitrateConfig["lowBitrate"] = true;
        const auto changed = yandexMusic.updateConfig(lowBitrateConfig);
        UNIT_ASSERT_EQUAL(changed, Player::ChangeConfigResult::CHANGED);
        track = yandexMusic.start();
        lowBitratePromise->get_future().get();
        YIO_LOG_INFO("Got request with low bitrate");

        UNIT_ASSERT(true);
    }

    Y_UNIT_TEST(testNormalizationWithTrack) {
        mockYaMusicServer->onMessage = [this](std::string msg) {
            const Json::Value request = parseJson(msg);
            if (request["action"].asString() == "ping" || request["action"].asString() == "auth" || request["action"].asString() == "feedback") {
                mockYaMusicServer->send(prepareYandexMusicSuccessResponse(request["reqId"].asString()));
            } else if (request["action"].asString() == "sync") {
                mockYaMusicServer->send(prepareSyncAddition(request["reqId"].asString(), request["data"]["index"].asInt()));
            } else if (request["action"].asString() == "url") {
                auto urlJson = parseJson(prepareUrl(request["reqId"].asString()));
                // actual test: add normalization values
                urlJson["data"]["r128"]["i"] = 0.123;
                urlJson["data"]["r128"]["tp"] = 1.234;
                mockYaMusicServer->send(jsonToString(urlJson));
            }
        };

        YandexMusic::Params params{YandexMusic::Params::Ssl::No, YandexMusic::Params::AutoPing::No};
        YandexMusic yandexMusic(getDeviceForTests(), params, getDefaultYandexMusicConfig(), []() {});
        YIO_LOG_INFO("wait for connect");
        mockYaMusicServer->waitUntilConnections(1, 0);
        YIO_LOG_INFO("wait for connect DONE");
        yandexMusic.setAuthData("test_uid", "test_session_id", "test_token", "test_device_id");
        YIO_LOG_INFO("auth data set");

        {
            const auto track = yandexMusic.start();
            UNIT_ASSERT(track->normalization.has_value());
            UNIT_ASSERT_DOUBLES_EQUAL(track->normalization->integratedLoudness, 0.123, 0.0001);
            UNIT_ASSERT_DOUBLES_EQUAL(track->normalization->truePeak, 1.234, 0.0001);
            UNIT_ASSERT_DOUBLES_EQUAL(track->normalization->truePeak, 1.234, 0.0001);
            UNIT_ASSERT_DOUBLES_EQUAL(track->normalization->targetLufs, AudioPlayer::Params::Normalization::DEFAULT_TARGET_LUFS, 0.0001);
        }

        {
            auto params = getDefaultYandexMusicConfig();
            params["normalizationTargetLufs"] = -12.0;
            yandexMusic.updateConfig(params);
            const auto track = yandexMusic.next();
            UNIT_ASSERT(track->normalization.has_value());
            UNIT_ASSERT_DOUBLES_EQUAL(track->normalization->integratedLoudness, 0.123, 0.0001);
            UNIT_ASSERT_DOUBLES_EQUAL(track->normalization->truePeak, 1.234, 0.0001);
            UNIT_ASSERT_DOUBLES_EQUAL(track->normalization->truePeak, 1.234, 0.0001);
            UNIT_ASSERT_DOUBLES_EQUAL(track->normalization->targetLufs, -12.0, 0.0001);
        }

    }
}
