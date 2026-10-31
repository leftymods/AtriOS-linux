#include <yandex_io/services/sound_initd/sound_init_endpoint.h>

#include <yandex_io/capabilities/file_player/interfaces/mocks/mock_i_file_player_capability.h>
#include <yandex_io/interfaces/device_state/mock/device_state_provider.h>
#include <yandex_io/libs/setup_parser/encoder_decoder.h>
#include <yandex_io/libs/setup_parser/sound_data_receiver.h>
#include <yandex_io/libs/setup_parser/sound_utils.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_server.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/setup_parser/setup_parser.h>
#include <yandex_io/libs/setup_parser/wifi_type.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <library/cpp/testing/unittest/env.h>
#include <library/cpp/testing/unittest/registar.h>

#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

namespace {

    using namespace quasar;
    using namespace quasar::SetupParser;
    using namespace quasar::SoundUtils;

    class ReceivedDataManager {
    public:
        std::promise<std::string> messagePromise;
        ReceivedDataManager() = default;
        void onDataReceived(const std::vector<unsigned char>& bytes)
        {
            std::cout << "ReceivedDataManager: data received!" << std::endl;
            messagePromise.set_value(bytesToString(bytes));
        }
        std::string getReceived()
        {
            std::future<std::string> futureMsg = messagePromise.get_future();
            std::future_status status = futureMsg.wait_for(std::chrono::seconds(3));
            if (status != std::future_status::ready) {
                return "";
            }
            return futureMsg.get();
        }
    };

    void test(std::string s, std::shared_ptr<YandexIO::IDevice> device)
    {
        std::vector<std::vector<std::int16_t>> samples = soundDataSourceGenerateSamples(0, stringToBytes(s), DEFAULT_SAMPLE_RATE);

        ReceivedDataManager receivedDataManager;

        SoundDataReceiver dataReceiver(device, SoundUtils::DEFAULT_SAMPLE_RATE);
        dataReceiver.onDataReceived = std::bind(&ReceivedDataManager::onDataReceived, &receivedDataManager, std::placeholders::_1);
        dataReceiver.start();

        for (const std::vector<std::int16_t>& sample : samples) {
            dataReceiver.write(sample);
        }
        UNIT_ASSERT_VALUES_EQUAL(s, receivedDataManager.getReceived());
        std::cout << "done" << std::endl;
    }

    Y_UNIT_TEST_SUITE_F(TestSoundInit, QuasarUnitTestFixture) {
        Y_UNIT_TEST(testCheckSum)
        {
            std::vector<unsigned char> payload = {2, 14, 77, 111, 98, 68, 101, 118, 73, 110, 116, 101, 114, 110, 101, 116, 15, 102, 111, 114, 32, 115, 105, 100, 105, 32, 115, 105, 116, 101, 115, 32, 18, 103, 114, 111, 119, 32, 100, 73, 68, 32, 99, 111, 115, 116, 32, 102, 103, 104, 32};
            std::vector<unsigned char> withCheckSum = addChecksum(payload);
            UNIT_ASSERT(isChecksumCorrect(withCheckSum));
            UNIT_ASSERT(isChecksumCorrect(addChecksum(stringToBytes("adsnlnsdkovdavlkads"))));
        }

        Y_UNIT_TEST(testSoundDataReceiver)
        {
            test("Hello", getDeviceForTests());
            test("Мама мыла раму", getDeviceForTests());
            test("qwerty", getDeviceForTests());
            test("!@#$%^&*()", getDeviceForTests());
        }

        void allPhasesTest(std::string s, int errorsPercent, std::shared_ptr<YandexIO::IDevice> device)
        {
            std::vector<std::vector<std::int16_t>> samples_ = soundDataSourceGenerateSamples(0, stringToBytes(s), DEFAULT_SAMPLE_RATE);
            std::vector<std::int16_t> samples((size_t)(SoundUtils::DEFAULT_SAMPLE_RATE * SoundUtils::DURATION));
            for (size_t i = 0; i < samples.size(); ++i) {
                samples[i] = (std::int16_t)rand();
            }
            for (const std::vector<std::int16_t>& sample : samples_) {
                samples.insert(samples.end(), sample.begin(), sample.end());
            }

            size_t good = 0;
            int total = (int)round(SoundUtils::DEFAULT_SAMPLE_RATE * SoundUtils::DURATION);

            std::vector<int> shifts = {0, 1, total - 1};
            for (int i = 0; i < 5; ++i) {
                shifts.push_back(rand() % total);
            }

            for (auto shift : shifts) {
                std::vector<std::int16_t> shifted(samples.begin() + shift, samples.end());
                int toModifyCount = shifted.size() * errorsPercent / 100;
                for (int i = 0; i < toModifyCount; ++i) {
                    shifted[rand() % shifted.size()] = (unsigned char)rand();
                }

                ReceivedDataManager receivedDataManager;
                SoundDataReceiver dataReceiver(device, SoundUtils::DEFAULT_SAMPLE_RATE);
                dataReceiver.write(samples);
                dataReceiver.onDataReceived = std::bind(&ReceivedDataManager::onDataReceived, &receivedDataManager, std::placeholders::_1);
                dataReceiver.start();
                if (s == receivedDataManager.getReceived()) {
                    good++;
                }
                dataReceiver.stop();
            }

            std::cout << "Passed " << good << " out of " << shifts.size();
            UNIT_ASSERT_VALUES_EQUAL(good, shifts.size());
        }

        Y_UNIT_TEST(testShift)
        {
            allPhasesTest("Hello my ", 45, getDeviceForTests());
        }

        struct ConnectParameters {
            std::vector<std::string> ssids;
            std::string password;
            std::string authCode;
            quasar::proto::WifiType wifiType = quasar::proto::WifiType::UNKNOWN_WIFI_TYPE;
            bool stopAccessPoint = true;
        };

        class MockAudioSourceClient: public YandexIO::IAudioSourceClient {
        public:
            void subscribeToChannels(YandexIO::RequestChannelType /*type*/) override{};
            void unsubscribeFromChannels() override{};
            void start() override{};
            void addListener(std::weak_ptr<Listener> listener) override {
                listener_ = std::move(listener);
            }
            void pushData(const YandexIO::ChannelsData& data) {
                auto listener = listener_.lock();
                UNIT_ASSERT(listener != nullptr);
                listener->onAudioData(data);
            }

        private:
            std::weak_ptr<Listener> listener_;
        };

        Y_UNIT_TEST(testSoundInit)
        {
            auto mockMetrica = createIpcServerForTests("metricad");
            auto mockWifid = createIpcServerForTests("wifid");
            auto mockYandexIO = createIpcServerForTests("iohub_services");

            auto mockDeviceStateProvider = std::make_shared<mock::DeviceStateProvider>();
            auto audioSource = std::make_shared<MockAudioSourceClient>();

            std::promise<bool> connected_;

            auto mockSetupd = createIpcServerForTests("setupd");
            mockSetupd->setMessageHandler([&](const auto& request, auto& /*connection*/) {
                if (request->has_setup_credentials_message()) {
                    auto msg = request->setup_credentials_message();
                    std::vector<unsigned char> credentialsBytes{
                        msg.setup_credentials().begin(),
                        msg.setup_credentials().end(),
                    };
                    const Credentials credentials = quasar::SetupParser::parseInitData(credentialsBytes);
                    UNIT_ASSERT(credentials.SSIDs[0] == "MobDevInternet");
                    UNIT_ASSERT(credentials.password == "password");
                    UNIT_ASSERT(credentials.tokenCode == "xToken");
                    UNIT_ASSERT(credentials.wifiType == quasar::WifiType::WIFI_TYPE_WPA);
                    UNIT_ASSERT(msg.source() == quasar::proto::SetupSource::SOUND);
                    connected_.set_value(true);
                }
            });

            mockMetrica->listenService();
            mockWifid->listenService();
            mockYandexIO->listenService();
            mockSetupd->listenService();

            auto filePlayerCapabilityMock = std::make_shared<YandexIO::MockIFilePlayerCapability>();
            SoundInitEndpoint soundInitEndpoint(getDeviceForTests(), ipcFactoryForTests(), mockDeviceStateProvider, audioSource, filePlayerCapabilityMock);

            std::vector<unsigned char> ssid = stringToBytes("MobDevInternet");
            std::vector<unsigned char> password = stringToBytes("password");
            std::vector<unsigned char> xToken = stringToBytes("xToken");
            std::vector<unsigned char> testMessage;
            testMessage.push_back(quasar::WifiType::WIFI_TYPE_WPA);
            testMessage.push_back(ssid.size());
            testMessage.insert(testMessage.end(), ssid.begin(), ssid.end());
            testMessage.push_back(password.size());
            testMessage.insert(testMessage.end(), password.begin(), password.end());
            testMessage.push_back(xToken.size());
            testMessage.insert(testMessage.end(), xToken.begin(), xToken.end());

            std::vector<std::vector<std::int16_t>> samples = soundDataSourceGenerateSamples(0, testMessage, DEFAULT_SAMPLE_RATE);
            std::vector<std::int16_t> buffer;
            for (const std::vector<std::int16_t>& sample : samples) {
                buffer.insert(buffer.end(), sample.begin(), sample.end());
            }

            auto deviceState = mock::defaultDeviceState();
            deviceState.configuration = DeviceState::Configuration::CONFIGURING;
            mockDeviceStateProvider->setDeviceState(deviceState);

            YandexIO::ChannelData channel;
            channel.data = std::move(buffer);
            channel.isForRecognition = true;
            audioSource->pushData({std::move(channel)});

            connected_.get_future().get();
        }

        void testSoundFromFile(const std::string& filename, std::shared_ptr<YandexIO::IDevice> device) {
            // tests if no SEGV

            std::promise<void> testDone;
            auto t = std::thread([filename, &testDone, device]() {
                {
                    ReceivedDataManager receivedDataManager;

                    SoundDataReceiver dataReceiver(device, SoundUtils::DEFAULT_SAMPLE_RATE);
                    std::promise<bool> resultPromise;
                    dataReceiver.onDataReceived = [&resultPromise](const std::vector<unsigned char>& data, int protocolVersion) {
                        YIO_LOG_INFO("LOL. onDataReceived:" << std::string(data.begin(), data.end()));
                        Y_UNUSED(protocolVersion);
                        resultPromise.set_value(true);
                    };
                    dataReceiver.onTransferError = [&resultPromise]() {
                        YIO_LOG_INFO("LOL. onTransferError");
                        resultPromise.set_value(false);
                    };
                    dataReceiver.onUnsupportedProtocol = [&resultPromise](int /*protocolVersion*/) {
                        YIO_LOG_INFO("LOL. onTransferError");
                        resultPromise.set_value(false);
                    };
                    dataReceiver.start();

                    std::cout << "File read" << std::endl;
                    std::ifstream ifs(filename, std::ios::binary);
                    ifs.unsetf(std::ios::skipws);
                    std::vector<unsigned char> byteBuf{std::istream_iterator<unsigned char>(ifs), {}};

                    dataReceiver.write(convert(byteBuf));

                    std::future<bool> futureMsg = resultPromise.get_future();
                    std::future_status status = futureMsg.wait_for(std::chrono::seconds(3));
                    UNIT_ASSERT(status == std::future_status::ready);
                    if (futureMsg.get()) {
                        std::cout << "RECEIVED MESSAGE" << std::endl;
                    } else {
                        std::cout << "NO MESSAGE FOUND" << std::endl;
                    }
                }
                testDone.set_value();
            });
            std::future<void> futureDone = testDone.get_future();
            futureDone.get();
            t.join();
        }

        Y_UNIT_TEST(testSoundInitFile) {
            testSoundFromFile(ArcadiaSourceRoot() + "/yandex_io/services/sound_initd/tests/test_data/test_soundinit.raw", getDeviceForTests());
            testSoundFromFile(ArcadiaSourceRoot() + "/yandex_io/services/sound_initd/tests/test_data/segfault-sound-begin.raw", getDeviceForTests());
            testSoundFromFile(ArcadiaSourceRoot() + "/yandex_io/services/sound_initd/tests/test_data/segfault-sound-begin-2.raw", getDeviceForTests());
            testSoundFromFile(ArcadiaSourceRoot() + "/yandex_io/services/sound_initd/tests/test_data/hang-sound-begin.raw", getDeviceForTests());
        }

        Y_UNIT_TEST(testSoundInitHash)
        {
            UNIT_ASSERT_VALUES_EQUAL(-22848, javaStyleStringHash("MobDevInternet"));
            UNIT_ASSERT_VALUES_EQUAL(22448, javaStyleStringHash("StationTEST(ASUS)"));
            UNIT_ASSERT_VALUES_EQUAL(-17676, javaStyleStringHash("StationTEST-ASUS"));
            UNIT_ASSERT_VALUES_EQUAL(-24766, javaStyleStringHash("StationTESTASUS"));
            UNIT_ASSERT_VALUES_EQUAL(-15181, javaStyleStringHash(
                                                 "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~ "));
            UNIT_ASSERT_VALUES_EQUAL(-16113, javaStyleStringHash("Латиница, лол"));
            UNIT_ASSERT_VALUES_EQUAL(-11843, javaStyleStringHash("😁😥✂🚾"));
            UNIT_ASSERT_VALUES_EQUAL(21509, javaStyleStringHash("MobDevInternet StationTEST(ASUS) StationTEST-ASUS StationTESTASUS "
                                                                "\"!\\\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\\\]"
                                                                "^_`abcdefghijklmnopqrstuvwxyz{|}~ \" Латиница, лол 😁😥✂🚾"));
        }

        Y_UNIT_TEST(testGetProtocolVersionFromFrequenciesNoCrash)
        {
            UNIT_ASSERT_EQUAL(getProtocolVersionFromFrequencies({0, 0}, 2, SoundUtils::TOLERANCE), -1);
        }

        Y_UNIT_TEST(testSoundDataReceiverNoSound)
        {
            test("", getDeviceForTests());
        }
    }

} /* anonymous namespace */
