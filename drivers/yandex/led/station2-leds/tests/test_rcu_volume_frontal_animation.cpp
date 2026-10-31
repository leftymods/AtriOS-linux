#include <smart_devices/platforms/yandexstation_2/platformd/leds/volume/rcu_volume_frontal_animation.h>

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/modules/leds/led_controller/null_led_controller.h>
#include <yandex_io/modules/leds/led_manager/ng/default_led_devices.h>
#include <yandex_io/modules/volume_manager/base/volume_manager.h>
#include <yandex_io/tests/testlib/test_utils.h>
#include <yandex_io/tests/testlib/null_sdk/null_sdk_interface.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <util/folder/path.h>

#include <cmath>
#include <sstream>
#include <vector>

using namespace quasar;
using namespace quasar::TestUtils;

namespace {
    unsigned char singlePixelGif[] = {
        0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
        0xff, 0x21, 0xf9, 0x04, 0x01, 0x0a, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
        0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3b};
    const int singlePixelGifSize = 43;

    auto proceed = [](const std::shared_ptr<Animation>& animation) { animation->updateTime(animation->getEndOfFrameTimePoint()); };

    struct Fixture: public QuasarUnitTestFixture {
        std::string basePath_;
        std::string volumePulseUpPath_ = "rcu_volume/volume_pulse/up/";
        std::string volumePulseDownPath_ = "rcu_volume/volume_pulse/down/";
        std::string volumeRotationUpPath_ = "rcu_volume/volume_rotation/up/";
        std::string volumeRotationDownPath_ = "rcu_volume/volume_rotation/down/";
        std::string volumeEdgePath_ = "rcu_volume/volume_edge/";

        Fixture() {
            basePath_ = tryGetRamDrivePath() + "/animations/";

            TFsPath(basePath_ + volumePulseUpPath_).MkDirs();
            TFsPath(basePath_ + volumePulseDownPath_).MkDirs();
            TFsPath(basePath_ + volumeRotationUpPath_).MkDirs();
            TFsPath(basePath_ + volumeRotationDownPath_).MkDirs();
            TFsPath(basePath_ + volumeEdgePath_).MkDirs();

            writeTempGif(basePath_ + "/sound_mute.gif", singlePixelGif, singlePixelGifSize);
            writeTempGif(basePath_ + volumeEdgePath_ + "0.gif", singlePixelGif, singlePixelGifSize);
            writeTempGif(basePath_ + volumeEdgePath_ + "10.gif", singlePixelGif, singlePixelGifSize);

            for (int i = 0; i <= 10; i++) {
                writeTempGif(basePath_ + volumePulseUpPath_ + std::to_string(i) + ".gif", singlePixelGif,
                             singlePixelGifSize);
                if (i != 0) {
                    writeTempGif(basePath_ + volumePulseDownPath_ + std::to_string(i) + ".gif", singlePixelGif,
                                 singlePixelGifSize);
                    writeTempGif(basePath_ + volumeRotationDownPath_ + std::to_string(i) + ".gif", singlePixelGif,
                                 singlePixelGifSize);
                }
                if (i != 10) {
                    writeTempGif(basePath_ + volumeRotationUpPath_ + std::to_string(i) + ".gif", singlePixelGif,
                                 singlePixelGifSize);
                }
            }
        }

        static void writeTempGif(std::string filename, unsigned char gif[], const int size) {
            std::ofstream myfile;
            myfile.open(filename, std::ios::binary);
            myfile.write(reinterpret_cast<char*>(gif), size);
        }

        ~Fixture() {
            TFsPath(basePath_).ForceDelete();
        }
    };

    class YandexStationFrontalControllerMock: public YandexStationFrontalController {
    public:
        YandexStationFrontalControllerMock()
            : YandexStationFrontalController("-", std::make_shared<ClockDisplayStateHolder>("/state_file.dat", std::make_shared<YandexIO::NullSDKInterface>()))
        {
        }
    };

    Y_UNIT_TEST_SUITE_F(testRcuVolumeFrontalAnimation, Fixture) {
        Y_UNIT_TEST(testSelectPulseUpAnimationOnStart) {
            auto frontalController = std::make_shared<YandexStationFrontalControllerMock>();

            for (int i = 1; i < 10; i++) {
                RcuVolumeFrontalAnimation rcuVolumeFrontalAnimation(basePath_, frontalController);
                rcuVolumeFrontalAnimation.updateAnimation((double)i / 10, false, true);

                auto gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());

                std::ostringstream expectedPath;
                expectedPath << basePath_ << volumePulseUpPath_ << std::to_string(i) << ".gif";
                UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
            }
        }

        Y_UNIT_TEST(testEdgeAnimation) {
            auto frontalController = std::make_shared<YandexStationFrontalControllerMock>();

            for (int i : {0, 10}) {
                RcuVolumeFrontalAnimation rcuVolumeFrontalAnimation(basePath_, frontalController);
                // Update only volume
                rcuVolumeFrontalAnimation.updateVolume((double)i / 10);
                // Update animation while volume hasn't changed
                rcuVolumeFrontalAnimation.updateAnimation((double)i / 10, false, true);

                auto gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());

                std::ostringstream expectedPath;
                expectedPath << basePath_ << volumeEdgePath_ << std::to_string(i) << ".gif";
                UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
            }
        }

        Y_UNIT_TEST(testRotationUpAnimation) {
            auto frontalController = std::make_shared<YandexStationFrontalControllerMock>();

            for (int i = 0; i < 10; i++) {
                RcuVolumeFrontalAnimation rcuVolumeFrontalAnimation(basePath_, frontalController);
                // Update only volume
                rcuVolumeFrontalAnimation.updateVolume((double)i / 10);
                // Volume up and update animation
                rcuVolumeFrontalAnimation.updateAnimation((double)(i + 1) / 10, false, true);

                auto gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());

                std::ostringstream expectedPath;
                expectedPath << basePath_ << volumeRotationUpPath_ << std::to_string(i) << ".gif";
                UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
            }
        }

        Y_UNIT_TEST(testRotationDownAnimation) {
            auto frontalController = std::make_shared<YandexStationFrontalControllerMock>();

            for (int i = 10; i > 0; i--) {
                RcuVolumeFrontalAnimation rcuVolumeFrontalAnimation(basePath_, frontalController);
                // Update only volume
                rcuVolumeFrontalAnimation.updateVolume((double)i / 10);
                // Volume down and update animation
                rcuVolumeFrontalAnimation.updateAnimation((double)(i - 1) / 10, false, true);

                auto gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());

                std::ostringstream expectedPath;
                expectedPath << basePath_ << volumeRotationDownPath_ << std::to_string(i) << ".gif";
                UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
            }
        }

        Y_UNIT_TEST(testPulseUpAnimation) {
            auto frontalController = std::make_shared<YandexStationFrontalControllerMock>();

            for (int i = 1; i <= 10; i++) {
                RcuVolumeFrontalAnimation rcuVolumeFrontalAnimation(basePath_, frontalController);
                // Update only volume
                rcuVolumeFrontalAnimation.updateVolume((double)i / 10);
                // Slightly volume up and update animation
                rcuVolumeFrontalAnimation.updateAnimation((double)i / 10 + 0.01, false, true);

                auto gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());

                std::ostringstream expectedPath;
                expectedPath << basePath_ << volumePulseUpPath_ << std::to_string(i) << ".gif";
                UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
            }
        }

        Y_UNIT_TEST(testPulseDownAnimation) {
            auto frontalController = std::make_shared<YandexStationFrontalControllerMock>();

            for (int i = 10; i > 0; i--) {
                RcuVolumeFrontalAnimation rcuVolumeFrontalAnimation(basePath_, frontalController);
                // Update only volume
                rcuVolumeFrontalAnimation.updateVolume((double)i / 10);
                // Slightly volume down and update animation
                rcuVolumeFrontalAnimation.updateAnimation((double)i / 10 - 0.01, false, true);

                auto gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());

                std::ostringstream expectedPath;
                expectedPath << basePath_ << volumePulseDownPath_ << std::to_string(i) << ".gif";
                UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
            }
        }

        Y_UNIT_TEST(testMiddleVolumeRotationUp) {
            auto frontalController = std::make_shared<YandexStationFrontalControllerMock>();

            for (int i = 1; i < 10; i++) {
                RcuVolumeFrontalAnimation rcuVolumeFrontalAnimation(basePath_, frontalController);
                // Update only volume
                rcuVolumeFrontalAnimation.updateVolume((double)i / 10 + 0.04);
                // Slightly volume down and update animation
                rcuVolumeFrontalAnimation.updateAnimation((double)i / 10 + 0.06, false, true);

                auto gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());

                std::ostringstream expectedPath;
                expectedPath << basePath_ << volumeRotationUpPath_ << std::to_string(i) << ".gif";
                UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
            }
        }

        Y_UNIT_TEST(testMiddleVolumeRotationDown) {
            auto frontalController = std::make_shared<YandexStationFrontalControllerMock>();

            for (int i = 10; i > 1; i--) {
                RcuVolumeFrontalAnimation rcuVolumeFrontalAnimation(basePath_, frontalController);
                // Update only volume
                rcuVolumeFrontalAnimation.updateVolume((double)i / 10 - 0.04);
                // Slightly volume down and update animation
                rcuVolumeFrontalAnimation.updateAnimation((double)i / 10 - 0.06, false, true);

                auto gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());

                std::ostringstream expectedPath;
                expectedPath << basePath_ << volumeRotationDownPath_ << std::to_string(i) << ".gif";
                UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
            }
        }

        Y_UNIT_TEST(testZeroVolume) {
            auto frontalController = std::make_shared<YandexStationFrontalControllerMock>();

            RcuVolumeFrontalAnimation rcuVolumeFrontalAnimation(basePath_, frontalController);
            // Update only volume
            rcuVolumeFrontalAnimation.updateVolume(0);
            // Slightly volume up and update animation
            rcuVolumeFrontalAnimation.updateAnimation(0.01, false, true);

            auto gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());

            std::ostringstream expectedPath;
            expectedPath << basePath_ << volumeRotationUpPath_ << "0.gif";
            UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
        }

        Y_UNIT_TEST(testMuteAnimation) {
            auto frontalController = std::make_shared<YandexStationFrontalControllerMock>();

            RcuVolumeFrontalAnimation rcuVolumeFrontalAnimation(basePath_, frontalController);
            // Mute sound
            rcuVolumeFrontalAnimation.updateAnimation(0.5, true, true);

            auto gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());

            std::ostringstream expectedPath;
            expectedPath << basePath_ << "sound_mute.gif";
            UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
        }

        Y_UNIT_TEST(testDoNotInterruptRotationAnimationWithPulse) {
            auto frontalController = std::make_shared<YandexStationFrontalControllerMock>();

            RcuVolumeFrontalAnimation rcuVolumeFrontalAnimation(basePath_, frontalController);
            // Update only volume
            rcuVolumeFrontalAnimation.updateVolume(0);
            // Volume up and update animation
            rcuVolumeFrontalAnimation.updateAnimation(0.1, false, true);

            auto gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());
            std::ostringstream expectedPath;
            expectedPath << basePath_ << volumeRotationUpPath_ << "0.gif";
            UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
            UNIT_ASSERT(!gifAnimation->finished());

            // Slightly volume up and update animation (pulse animation)
            rcuVolumeFrontalAnimation.updateAnimation(0.12, false, true);

            gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());
            // Rotation animation is not finished yet, pulse shouldn't interrupt it
            UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
            UNIT_ASSERT(!gifAnimation->finished());
        }

        Y_UNIT_TEST(testDoNotInterruptPulseAnimationWithEdge) {
            auto frontalController = std::make_shared<YandexStationFrontalControllerMock>();

            RcuVolumeFrontalAnimation rcuVolumeFrontalAnimation(basePath_, frontalController);
            // Update only volume
            rcuVolumeFrontalAnimation.updateVolume(0.99);
            // Slightly volume up and update animation
            rcuVolumeFrontalAnimation.updateAnimation(1, false, true);

            auto gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());
            std::ostringstream expectedPath;
            expectedPath << basePath_ << volumePulseUpPath_ << "10.gif";
            UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
            UNIT_ASSERT(!gifAnimation->finished());

            // Update 1 again (edge animation)
            rcuVolumeFrontalAnimation.updateAnimation(1, false, true);

            gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());
            // Rotation animation is not finished yet, pulse shouldn't interrupt it
            UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
            UNIT_ASSERT(!gifAnimation->finished());
        }

        Y_UNIT_TEST(testDontShowPulseAnimation) {
            Animation::TimePoint animationStart = std::chrono::steady_clock::now(); // could be anything

            auto frontalController = std::make_shared<YandexStationFrontalControllerMock>();
            bool showPulse = false;

            for (int i = 0; i < 9; i++) {
                RcuVolumeFrontalAnimation rcuVolumeFrontalAnimation(basePath_, frontalController);
                // Update only volume
                rcuVolumeFrontalAnimation.updateVolume((double)i / 10);
                // Volume up and update animation
                rcuVolumeFrontalAnimation.updateAnimation((double)(i + 1) / 10, false, showPulse);

                auto gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());

                std::ostringstream expectedPath;
                expectedPath << basePath_ << volumeRotationUpPath_ << std::to_string(i) << ".gif";
                UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
                UNIT_ASSERT(!gifAnimation->finished());

                gifAnimation->startAnimationFrom(animationStart);
                // Finish rotation animation
                proceed(gifAnimation);

                // Slightly volume up and update animation
                rcuVolumeFrontalAnimation.updateAnimation((double)(i + 1) / 10 + 0.01, false, showPulse);

                gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());

                // Animation shouldn't have changed to pulse
                UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), expectedPath.str());
                UNIT_ASSERT(gifAnimation->finished());
            }
        }

        Y_UNIT_TEST(testShowPulseAnimation) {
            Animation::TimePoint animationStart = std::chrono::steady_clock::now(); // could be anything

            auto frontalController = std::make_shared<YandexStationFrontalControllerMock>();
            bool showPulse = true;

            for (int i = 0; i < 9; i++) {
                RcuVolumeFrontalAnimation rcuVolumeFrontalAnimation(basePath_, frontalController);
                // Update only volume
                rcuVolumeFrontalAnimation.updateVolume((double)i / 10);
                // Volume up and update animation
                rcuVolumeFrontalAnimation.updateAnimation((double)(i + 1) / 10, false, showPulse);

                auto gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());

                std::ostringstream rotationPath;
                rotationPath << basePath_ << volumeRotationUpPath_ << std::to_string(i) << ".gif";
                UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), rotationPath.str());
                UNIT_ASSERT(!gifAnimation->finished());
                gifAnimation->startAnimationFrom(animationStart);
                // Finish rotation animation
                proceed(gifAnimation);

                // Slightly volume up and update animation
                rcuVolumeFrontalAnimation.updateAnimation((double)(i + 1) / 10 + 0.01, false, showPulse);

                gifAnimation = std::static_pointer_cast<GifAnimation>(rcuVolumeFrontalAnimation.getCurrentAnimation());

                // Animation should have changed to pulse
                std::ostringstream pulsePath;
                pulsePath << basePath_ << volumePulseUpPath_ << std::to_string(i + 1) << ".gif";
                UNIT_ASSERT_EQUAL(gifAnimation->getFilepath(), pulsePath.str());
                UNIT_ASSERT(!gifAnimation->finished());
            }
        }
    }
} // namespace
