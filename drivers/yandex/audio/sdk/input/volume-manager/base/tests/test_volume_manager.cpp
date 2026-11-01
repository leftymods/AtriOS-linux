#include <yandex_io/modules/volume_manager/base/volume_manager.h>

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/telemetry/null/null_metrica.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>
#include <yandex_io/tests/testlib/null_sdk/null_sdk_interface.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <library/cpp/testing/gmock_in_unittest/gmock.h>
#include <library/cpp/testing/unittest/registar.h>

#include <util/folder/path.h>

#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using namespace quasar;

using namespace std::chrono_literals;

namespace {

    constexpr int INITIAL_VOLUME = 5;

    class VolumeManagerTestImpl: public VolumeManager {
    public:
        VolumeManagerTestImpl(std::shared_ptr<YandexIO::IDevice> device, std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory, std::shared_ptr<YandexIO::SDKInterface> sdk, std::string currentVolumeFilename, std::string currentMuteStateFilename, std::chrono::milliseconds periodOfSetting)
            : VolumeManager(std::move(device),
                            std::move(ipcFactory),
                            std::move(sdk),
                            std::move(currentVolumeFilename),
                            std::move(currentMuteStateFilename),
                            periodOfSetting)
        {
        }

        void checkLastVolume(int requiredVolume) {
            std::unique_lock<std::mutex> lock(mutex_);
            CV_.wait(lock, [this, requiredVolume] {
                return requiredVolume == lastVolume_;
            });
        }

        void setOnAlarmStoppedCb(std::function<void()> callback) {
            onAlarmStoppedCb_ = std::move(callback);
        }

        bool checkVolumes(const std::vector<int>& requiredVolumes) {
            std::unique_lock<std::mutex> lock(mutex_);
            CV_.wait(lock, [this, &requiredVolumes] {
                return setVolumes_.size() >= requiredVolumes.size();
            });

            /*
             * Just logging values, nothing functional
             */
            {
                std::stringstream ss;
                for (const auto& item : requiredVolumes) {
                    ss << item << ", ";
                }
                YIO_LOG_INFO("Required volumes " << ss.str());
            }
            {
                std::stringstream ss;
                for (const auto& item : setVolumes_) {
                    ss << item << ", ";
                }
                YIO_LOG_INFO("Set volumes      " << ss.str());
            }

            return requiredVolumes == setVolumes_;
        }

    protected:
        void onAlarmStopped(AlarmType alarmType, bool hasRemainingMedia) override {
            VolumeManager::onAlarmStopped(alarmType, hasRemainingMedia);
            if (onAlarmStoppedCb_) {
                onAlarmStoppedCb_();
            }
        }

        void setVolumeImplementation(int platformVolume) override {
            std::lock_guard<std::mutex> guard(mutex_);
            YIO_LOG_DEBUG("Setting volume " << platformVolume);
            setVolumes_.push_back(platformVolume);
            lastVolume_ = platformVolume;
            CV_.notify_all();
        };

        int minVolume() override {
            return 0;
        }
        int maxVolume() override {
            return MAX_ALICE_VOLUME;
        };
        int volumeStep() override {
            return 1;
        }
        int scaleFromAlice(int aliceVolume) override {
            return aliceVolume;
        };
        int scaleToAlice(int platformVolume) override {
            return platformVolume;
        };
        int initialVolume() override {
            return INITIAL_VOLUME;
        };

    private:
        std::function<void()> onAlarmStoppedCb_;
        std::vector<int> setVolumes_;
        int lastVolume_{-1};
        std::mutex mutex_;
        quasar::SteadyConditionVariable CV_;
    };

    class VolumeManagerFixtureBase: public QuasarUnitTestFixture {
    public:
        using Base = QuasarUnitTestFixture;

        void SetUp(NUnitTest::TTestContext& context) override {
            Base::SetUp(context);

            quasarDirPath = JoinFsPaths(tryGetRamDrivePath(), "quasarDir-" + quasar::makeUUID());
            currentVolumeFilename = JoinFsPaths(quasarDirPath, "currentVolumeFilename.dat");
            currentMuteStateFilename = JoinFsPaths(quasarDirPath, "currentMuteStateFilename.dat");

            removeFileStorages();
            quasarDirPath.MkDirs();

            auto& config = getDeviceForTests()->configuration()->getMutableConfig(testGuard);
            config["alarmd"]["startAlarmUserVolume"] = 2;
            config["alarmd"]["finishAlarmUserVolume"] = 7;
            config["alarmd"]["alarmVolumeStepMs"] = 0;
            config["alarmd"]["minimumReminderUserVolume"] = MIN_REMINDER_ALICE_VOLUME;

            instance = std::make_shared<YandexIO::NullSDKInterface>();
        }

        void TearDown(NUnitTest::TTestContext& context) override {
            removeFileStorages();

            Base::TearDown(context);
        }

        void removeFileStorages() {
            TFsPath(quasarDirPath).ForceDelete();
        }

    protected:
        TFsPath quasarDirPath;
        std::string currentVolumeFilename;
        std::string currentMuteStateFilename;

        YandexIO::Configuration::TestGuard testGuard;
        std::shared_ptr<YandexIO::SDKInterface> instance;
        static constexpr int MIN_REMINDER_ALICE_VOLUME = 7;
    };

    class VolumeManagerInstantFixture: public VolumeManagerFixtureBase {
    public:
        using Base = VolumeManagerFixtureBase;

        void SetUp(NUnitTest::TTestContext& context) override {
            Base::SetUp(context);

            volumeManager = std::make_shared<VolumeManagerTestImpl>(
                getDeviceForTests(), ipcFactoryForTests(), instance, currentVolumeFilename, currentMuteStateFilename, 0ms);
        }

        void TearDown(NUnitTest::TTestContext& context) override {
            Base::TearDown(context);
        }

        std::shared_ptr<VolumeManagerTestImpl> volumeManager;
    };

    class VolumeManagerDelayedFixture: public VolumeManagerFixtureBase {
    public:
        using Base = VolumeManagerFixtureBase;

        void SetUp(NUnitTest::TTestContext& context) override {
            Base::SetUp(context);

            volumeManager = std::make_shared<VolumeManagerTestImpl>(
                getDeviceForTests(), ipcFactoryForTests(), instance, currentVolumeFilename, currentMuteStateFilename, 1ms);
        }

        void TearDown(NUnitTest::TTestContext& context) override {
            Base::TearDown(context);
        }

        std::shared_ptr<VolumeManagerTestImpl> volumeManager;
    };

    class TelemetryMock: public NullMetrica {
    public:
        MOCK_METHOD(void, reportEvent, (const std::string&, const std::string&, ITelemetry::Flags), (override));
        MOCK_METHOD(void, putAppEnvironmentValue, (const std::string&, const std::string&), (override));
    };

} /* anonymous namespace */

Y_UNIT_TEST_SUITE(TestVolumeManager) {

    Y_UNIT_TEST_F(testInstantVolumeManagerManualCommands, VolumeManagerInstantFixture) {
        std::vector<int> requiredVolumes{5, 6, 0, 6, 0, 5, 7, 3, 0, 10};

        // INITIAL_VALUE must be set
        volumeManager->start();
        volumeManager->checkLastVolume(5);

        // Volume must increase by 1
        volumeManager->manualVolumeUp();
        // Toggle mute state twice, but with other methods, must set 0 and return back
        volumeManager->checkLastVolume(6);
        volumeManager->manualMute();
        volumeManager->checkLastVolume(0);
        volumeManager->manualUnmute();
        volumeManager->checkLastVolume(6);
        volumeManager->manualMute();
        volumeManager->checkLastVolume(0);
        // After volume down we must return value we had before mute minus one
        volumeManager->manualVolumeDown();
        volumeManager->checkLastVolume(5);
        // Volume changes (both positive and negative)
        volumeManager->manualVolumeChange(2);
        volumeManager->checkLastVolume(7);
        volumeManager->manualVolumeChange(-4);
        volumeManager->checkLastVolume(3);
        // Going out of scales
        volumeManager->manualVolumeChange(-4);
        volumeManager->checkLastVolume(0);
        volumeManager->manualVolumeChange(11);
        volumeManager->checkLastVolume(10);

        UNIT_ASSERT(volumeManager->checkVolumes(requiredVolumes));
    }

    Y_UNIT_TEST_F(testInstantVolumeManagerTimer, VolumeManagerInstantFixture) {
        std::vector<int> requiredVolumes{5, 2, 5};
        std::vector<int> requiredVolumes2{5, 2, 5, 2};

        // INITIAL_VALUE must be set
        volumeManager->start();
        volumeManager->checkLastVolume(5);

        // TODO: now timer volume is hardcoded as 5, so we manually set different value for now.
        //  Manual set can be deleted when timer volume will be able to be parametrized.
        const std::shared_ptr<YandexIO::AliceVolumeSetter> aliceVolumeSetter = volumeManager;
        aliceVolumeSetter->setVolume(2);
        volumeManager->checkLastVolume(2);
        const std::shared_ptr<YandexIO::AlarmObserver> alarmObserver = volumeManager;

        alarmObserver->onAlarmEnqueued(YandexIO::AlarmObserver::AlarmType::TIMER, "testAlarmId");
        volumeManager->checkLastVolume(5);
        UNIT_ASSERT(volumeManager->checkVolumes(requiredVolumes));

        alarmObserver->onAlarmStopped(YandexIO::AlarmObserver::AlarmType::TIMER, false);
        volumeManager->checkLastVolume(2);
        UNIT_ASSERT(volumeManager->checkVolumes(requiredVolumes2));
    }

    Y_UNIT_TEST_F(testInstantVolumeManagerUnmuteAtZero, VolumeManagerInstantFixture) {
        std::vector<int> requiredVolumes{5, 4, 3, 2, 1, 0, 1};

        // INITIAL_VALUE must be set
        volumeManager->start();
        volumeManager->checkLastVolume(5);

        // Manually reduce volume till zero, so it will have zero volume but still will be unmuted
        volumeManager->manualVolumeDown();
        volumeManager->checkLastVolume(4);
        volumeManager->manualVolumeDown();
        volumeManager->checkLastVolume(3);
        volumeManager->manualVolumeDown();
        volumeManager->checkLastVolume(2);
        volumeManager->manualVolumeDown();
        volumeManager->checkLastVolume(1);
        volumeManager->manualVolumeDown();
        volumeManager->checkLastVolume(0);
        // Unmute, volume should be at least 1
        volumeManager->manualUnmute();
        volumeManager->checkLastVolume(1);

        UNIT_ASSERT(volumeManager->checkVolumes(requiredVolumes));
    }

    Y_UNIT_TEST_F(testInstantVolumeManagerReminder, VolumeManagerInstantFixture) {
        std::vector<int> requiredVolumes{5, MIN_REMINDER_ALICE_VOLUME, 5};

        // INITIAL_VALUE must be set
        volumeManager->start();
        volumeManager->checkLastVolume(5);

        std::shared_ptr<YandexIO::SDKStateObserver> sdkStateObserver = volumeManager;

        {
            YandexIO::SDKState sdkState;
            sdkState.isReminderPlaying = true;
            sdkStateObserver->onSDKState(sdkState);
        }

        volumeManager->checkLastVolume(MIN_REMINDER_ALICE_VOLUME);

        {
            YandexIO::SDKState sdkState;
            sdkState.isReminderPlaying = false;
            sdkStateObserver->onSDKState(sdkState);
        }

        volumeManager->checkLastVolume(5);

        UNIT_ASSERT(volumeManager->checkVolumes(requiredVolumes));
    }

    Y_UNIT_TEST_F(testInstantVolumeManagerVolumeLimitation, VolumeManagerInstantFixture) {
        std::vector<int> requiredVolumes{5, 10, 7, 7, 6, 10};

        // INITIAL_VALUE must be set
        volumeManager->start();
        volumeManager->checkLastVolume(5);

        volumeManager->manualSetVolume(10);
        volumeManager->checkLastVolume(10);
        volumeManager->setAliceVolumeMaxLimit(7);
        volumeManager->checkLastVolume(7);
        volumeManager->manualSetVolume(8); // 7 must be set because we set the same value by default

        std::this_thread::sleep_for(5ms);

        volumeManager->manualSetVolume(6);
        volumeManager->checkLastVolume(6);
        volumeManager->setAliceVolumeMaxLimit(std::nullopt);
        volumeManager->manualSetVolume(10);
        volumeManager->checkLastVolume(10);

        UNIT_ASSERT(volumeManager->checkVolumes(requiredVolumes));
    }

    Y_UNIT_TEST_F(testInstantVolumeManagerVolumeModeManual, VolumeManagerInstantFixture) {
        volumeManager->start();
        volumeManager->checkLastVolume(5);

        volumeManager->manualSetVolume(7);
        volumeManager->checkLastVolume(7);

        volumeManager->enableStashVolumeMode(3);
        volumeManager->checkLastVolume(3);

        volumeManager->disableStashVolumeMode();

        volumeManager->checkLastVolume(7);

        volumeManager->enableStashVolumeMode(3);

        volumeManager->manualVolumeUp();

        volumeManager->disableStashVolumeMode();

        volumeManager->checkLastVolume(4);
    }

    Y_UNIT_TEST_F(testDelayedVolumeManagerManualCommands, VolumeManagerDelayedFixture) {
        volumeManager->start();
        volumeManager->checkLastVolume(5);

        volumeManager->manualVolumeDown();
        volumeManager->checkLastVolume(4);

        volumeManager->manualVolumeUp();
        volumeManager->manualVolumeUp();
        volumeManager->checkLastVolume(6);

        volumeManager->manualVolumeDown();
        volumeManager->manualVolumeUp();
        volumeManager->checkLastVolume(6);

        volumeManager->manualVolumeChange(10);
        volumeManager->manualVolumeDown();
        volumeManager->checkLastVolume(9);

        volumeManager->manualMute();
        volumeManager->checkLastVolume(0);

        volumeManager->manualUnmute();
        volumeManager->checkLastVolume(9);
    }

    Y_UNIT_TEST_F(testDelayedVolumeManagerAliceVolumeSetter, VolumeManagerDelayedFixture) {
        volumeManager->start();
        volumeManager->checkLastVolume(5);

        const std::shared_ptr<YandexIO::AliceVolumeSetter> aliceVolumeSetter = volumeManager;

        aliceVolumeSetter->setVolume(2);
        volumeManager->checkLastVolume(2);

        aliceVolumeSetter->volumeUp();
        volumeManager->checkLastVolume(3);
        aliceVolumeSetter->volumeDown();
        volumeManager->checkLastVolume(2);

        aliceVolumeSetter->mute();
        volumeManager->checkLastVolume(0);
        aliceVolumeSetter->unmute();
        volumeManager->checkLastVolume(2);
    }

    Y_UNIT_TEST_F(testDelayedVolumeManagerAlarm, VolumeManagerDelayedFixture) {
        volumeManager->start();
        volumeManager->checkLastVolume(5);

        const std::shared_ptr<YandexIO::AlarmObserver> alarmObserver = volumeManager;

        alarmObserver->onAlarmStarted(YandexIO::AlarmObserver::AlarmType::CLASSIC_ALARM);
        volumeManager->checkLastVolume(7);

        alarmObserver->onAlarmStopped(YandexIO::AlarmObserver::AlarmType::CLASSIC_ALARM, true);
        volumeManager->checkLastVolume(5);
    }

    Y_UNIT_TEST_F(testDelayedVolumeManagerAlarmManualChange, VolumeManagerDelayedFixture) {
        volumeManager->start();
        volumeManager->checkLastVolume(5);

        const std::shared_ptr<YandexIO::AlarmObserver> alarmObserver = volumeManager;

        alarmObserver->onAlarmStarted(YandexIO::AlarmObserver::AlarmType::CLASSIC_ALARM);
        volumeManager->checkLastVolume(7);

        volumeManager->manualVolumeDown();
        volumeManager->checkLastVolume(6);

        alarmObserver->onAlarmStopped(YandexIO::AlarmObserver::AlarmType::CLASSIC_ALARM, true);
        alarmObserver->onAlarmStopRemainingMedia();
        volumeManager->checkLastVolume(6);
    }

    MATCHER_P4(VerifyMetricJson, prevVolume, newVolume, muted, source, "description") {
        *result_listener << "Input json: " << arg << '\n';
        const auto json = parseJson(arg);
        if (source != json["source"].asString()) {
            *result_listener << "Invalid source. Expected: " << source << '\n';
            return false;
        }
        if (newVolume != json["alice_volume"].asInt()) {
            *result_listener << "Invalid Volume. Expected: " << newVolume << '\n';
            return false;
        }
        if (prevVolume != json["prev_alice_volume"].asInt()) {
            *result_listener << "Invalid Prev Volume. Expected: " << prevVolume << '\n';
            return false;
        }
        if (muted != json["muted"].asBool()) {
            *result_listener << "Invalid Volume Mute. Expected: " << muted << '\n';
            return false;
        }
        return true;
    }

    Y_UNIT_TEST_F(testMetrics, VolumeManagerFixtureBase) {
        using testing::_;
        const auto telemetry = std::make_shared<TelemetryMock>();
        const auto device = std::make_shared<YandexIO::Device>(
            QuasarUnitTestFixture::makeTestDeviceId(),
            getDeviceForTests()->sharedConfiguration(),
            telemetry,
            QuasarUnitTestFixture::makeTestHAL());

        const auto factory = ipcFactoryForTests();
        const auto manager = std::make_shared<VolumeManagerTestImpl>(device, factory, instance, currentVolumeFilename, currentMuteStateFilename, 0ms);
        const std::shared_ptr<YandexIO::AliceVolumeSetter> aliceVolumeSetter = manager;
        {
            testing::InSequence seq;
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "5"));
            manager->start();
            manager->checkLastVolume(5);
        }

        // test AliceVolumeSetter api
        {
            testing::InSequence seq;
            // voice functions
            EXPECT_CALL(*telemetry, reportEvent("volumeChanged", VerifyMetricJson(5, 1, false, "voice"), _));
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "1"));
            EXPECT_CALL(*telemetry, reportEvent("volumeChanged", VerifyMetricJson(1, 7, false, "voice"), _));
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "7"));
            EXPECT_CALL(*telemetry, reportEvent("volumeChanged", VerifyMetricJson(7, 8, false, "voice"), _));
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "8"));
            EXPECT_CALL(*telemetry, reportEvent("volumeChanged", VerifyMetricJson(8, 7, false, "voice"), _));
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "7"));
            EXPECT_CALL(*telemetry, reportEvent("volumeChanged", VerifyMetricJson(7, 7, true, "voice"), _));
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "0"));
            EXPECT_CALL(*telemetry, reportEvent("volumeChanged", VerifyMetricJson(7, 7, false, "voice"), _));
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "7"));

            // voice functions
            aliceVolumeSetter->setVolume(1);
            manager->checkLastVolume(1);
            aliceVolumeSetter->setVolume(7);
            manager->checkLastVolume(7);
            aliceVolumeSetter->volumeUp();
            manager->checkLastVolume(8);
            aliceVolumeSetter->volumeDown();
            manager->checkLastVolume(7);
            aliceVolumeSetter->mute();
            manager->checkLastVolume(0);
            aliceVolumeSetter->unmute();
            manager->checkLastVolume(7);
        }

        {
            testing::InSequence seq;
            // Make sure that metrics are sent only once when volume does not change
            EXPECT_CALL(*telemetry, reportEvent("volumeChanged", VerifyMetricJson(7, 1, false, "voice"), _));
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "1"));

            aliceVolumeSetter->setVolume(1);
            aliceVolumeSetter->setVolume(1);
            aliceVolumeSetter->setVolume(1);
            aliceVolumeSetter->setVolume(1);
            aliceVolumeSetter->setVolume(1);
            manager->checkLastVolume(1);
        }

        {
            testing::InSequence seq;
            // bluetooth volume
            EXPECT_CALL(*telemetry, reportEvent("volumeChanged", VerifyMetricJson(1, 2, false, "bluetooth"), _));
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "2"));
            const std::shared_ptr<YandexIO::BluetoothObserver> btObserver = manager;
            btObserver->onChangeVolumeAVRCP(20);
            manager->checkLastVolume(2);
        }
        {
            testing::InSequence seq;
            // buttons volume change/ other api
            EXPECT_CALL(*telemetry, reportEvent("volumeChanged", VerifyMetricJson(2, 5, false, "other"), _));
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "5"));
            EXPECT_CALL(*telemetry, reportEvent("volumeChanged", VerifyMetricJson(5, 4, false, "button"), _));
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "4"));
            EXPECT_CALL(*telemetry, reportEvent("volumeChanged", VerifyMetricJson(4, 5, false, "button"), _));
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "5"));
            EXPECT_CALL(*telemetry, reportEvent("volumeChanged", VerifyMetricJson(5, 10, false, "button"), _));
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "10"));
            EXPECT_CALL(*telemetry, reportEvent("volumeChanged", VerifyMetricJson(10, 10, true, "button"), _));
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "0"));
            EXPECT_CALL(*telemetry, reportEvent("volumeChanged", VerifyMetricJson(10, 10, false, "button"), _));
            EXPECT_CALL(*telemetry, putAppEnvironmentValue("currentVolume", "10"));
            // set
            manager->manualSetVolume(5, "other");
            manager->checkLastVolume(5);
            // buttons
            manager->manualVolumeDown();
            manager->checkLastVolume(4);
            manager->manualVolumeUp();
            manager->checkLastVolume(5);
            manager->manualSetVolume(10);
            manager->checkLastVolume(10);
            manager->manualMute();
            manager->checkLastVolume(0);

            manager->manualUnmute();
            manager->checkLastVolume(10);
            std::this_thread::sleep_for(10ms);
        }
    }

}
