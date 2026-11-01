#include <smart_devices/platforms/yandexstation_2/platformd/leds/volume/volume_frontal_animation.h>

#include <library/cpp/testing/unittest/registar.h>

#include <yandex_io/modules/leds/led_manager/led_animator.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_conductor.h>

namespace {
    class NullYandexStationHeadController: public quasar::LedController {
    public:
        NullYandexStationHeadController() = default;

        int getWidth() const override {
            return 1;
        }

        int getHeight() const override {
            return 1;
        }

        void clearFrame() override {
        }

        void drawFrame(const quasar::LedCircle& /* frame */) override {
        }

        quasar::rgbw_color readColor(const std::string& /* colorString */) override {
            return {};
        }

        int getLedCount() const override {
            return 0;
        }
    };

    class NullYandexStationFrontalController: public YandexStationFrontalController {
    public:
        NullYandexStationFrontalController() = default;

        void drawBuffer(const Frame& /* frame */) override {
        }
    };
} // namespace

Y_UNIT_TEST_SUITE(testVolumeFrontalAnimation) {

    Y_UNIT_TEST(testEqualsNear1) {
        UNIT_ASSERT_EQUAL(equalsNear1(0.0, 0.000000001), false);
        UNIT_ASSERT_EQUAL(equalsNear1(0.0, 0.0), true);
        UNIT_ASSERT_EQUAL(equalsNear1(1.0, 0.999999999), false);
        UNIT_ASSERT_EQUAL(equalsNear1(1.0, 1.0), true);
    } // Y_UNIT_TEST(testEqualsNear1)

    Y_UNIT_TEST(testCreateUpdateVolumeAndPlay) {
        auto nullYandexStationFrontalController = std::make_shared<NullYandexStationFrontalController>();
        auto nullYandexStationHeadController = std::make_shared<NullYandexStationHeadController>();

        unsigned char fiveFrameGif[] = {
            0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00,
            0x01, 0x00, 0x80, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF,
            0x21, 0xF9, 0x04, 0x00, 0x02, 0x00, 0xFF, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00,
            0x21, 0xF9, 0x04, 0x01, 0x02, 0x00, 0x01, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00,
            0x21, 0xF9, 0x04, 0x01, 0x02, 0x00, 0x01, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00,
            0x21, 0xF9, 0x04, 0x01, 0x02, 0x00, 0x01, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00,
            0x21, 0xF9, 0x04, 0x01, 0x02, 0x00, 0x01, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3B};
        const int fiveFrameGifLength = 19 + (23 * 5) + 1;

        std::ofstream myfile;
        myfile.open("./volumeFrontalAnimation.gif", std::ios::binary);
        myfile.write(reinterpret_cast<char*>(fiveFrameGif), fiveFrameGifLength);
        myfile.close();

        auto gif = quasar::loadGif("./volumeFrontalAnimation.gif");
        auto volumeFrontalAnimation = std::make_shared<VolumeFrontalAnimation>("./volumeFrontalAnimation.gif", nullYandexStationFrontalController);
        volumeFrontalAnimation->updateAnimation(0.5);
        quasar::LedAnimator animator(quasar::LedPattern::getIdlePattern(1, nullYandexStationHeadController), 2);
        auto conductor = std::make_shared<DefaultAnimationConductor>(volumeFrontalAnimation, SubstitutionType::FOREGROUND);

        animator.play(conductor);
    } // Y_UNIT_TEST(testCreateUpdateVolumeAndPlay)

}
