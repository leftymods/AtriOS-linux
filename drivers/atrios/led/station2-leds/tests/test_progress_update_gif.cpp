#include <smart_devices/platforms/yandexstation_2/platformd/leds/update/progress_update_gif.h>

#include <yandex_io/modules/leds/led_manager/led_animator.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <library/cpp/testing/unittest/registar.h>

namespace {
    class NullYandexStationFrontalController: public YandexStationFrontalController {
    public:
        NullYandexStationFrontalController() {
        }

        void drawBuffer(const Frame& /* frame */) override {
        }
    };

    class NullYandexStationHeadController: public quasar::LedController {
    public:
        NullYandexStationHeadController() {
        }

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
} // namespace

class GifFixture: public NUnitTest::TBaseFixture {
public:
    void SetUp(NUnitTest::TTestContext& context) override {
        NUnitTest::TBaseFixture::SetUp(context);
        frontalController_ = std::make_shared<NullYandexStationFrontalController>();
        progressUpdateGif_ = std::make_shared<ProgressUpdateGif>(
            frontalController_,
            ArcadiaSourceRoot() + "/smart_devices/platforms/yandexstation_2/platformd/leds/tests/gif/test.gif");
        UNIT_ASSERT(progressUpdateGif_);
    }

    void TearDown(NUnitTest::TTestContext& context) override {
        NUnitTest::TBaseFixture::TearDown(context);
        progressUpdateGif_.reset();
        frontalController_.reset();
    }

    ProgressUpdateGif& gif() {
        return *progressUpdateGif_;
    }

private:
    std::shared_ptr<NullYandexStationFrontalController> frontalController_;
    std::shared_ptr<ProgressUpdateGif> progressUpdateGif_;
};

Y_UNIT_TEST_SUITE(testProgressUpdateGif) {
    Y_UNIT_TEST_F(testCreate, GifFixture) {
        UNIT_ASSERT_EQUAL(gif().currentFrame(), 0);
    }

    Y_UNIT_TEST_F(testUpdateAndDraw, GifFixture) {
        auto nullYandexStationFrontalController = std::make_shared<NullYandexStationFrontalController>();
        auto nullYandexStationHeadController = std::make_shared<NullYandexStationHeadController>();

        auto progressUpdateGif = std::make_shared<ProgressUpdateGif>(
            nullYandexStationFrontalController,
            ArcadiaSourceRoot() + "/smart_devices/platforms/yandexstation_2/platformd/leds/tests/gif/test.gif");

        Animation::TimePoint animationStart = std::chrono::steady_clock::now(); // could be anything
        gif().startAnimationFrom(animationStart);

        gif().updateTime(animationStart + std::chrono::seconds(1));
        gif().setProgress(0.0);
        gif().drawCurrentFrame();
        UNIT_ASSERT_EQUAL(gif().currentFrame(), 0);
        gif().setProgress(10.0);
        UNIT_ASSERT_EQUAL(gif().currentFrame(), 12);
    }

    Y_UNIT_TEST_F(testOverflowProgressDoesNotKill, GifFixture) {
        Animation::TimePoint animationStart = std::chrono::steady_clock::now(); // could be anything
        gif().startAnimationFrom(animationStart);

        gif().updateTime(animationStart);
        gif().setProgress(300.0);
        gif().drawCurrentFrame();
        UNIT_ASSERT_EQUAL(gif().currentFrame(), gif().getFrameCount() - 1);
    }

    Y_UNIT_TEST_F(testGifNeverFinished, GifFixture) {
        Animation::TimePoint animationStart = std::chrono::steady_clock::now(); // could be anything
        gif().startAnimationFrom(animationStart);

        gif().updateTime(animationStart);
        gif().setProgress(100.0);
        gif().drawCurrentFrame();
        UNIT_ASSERT(!gif().finished());
        UNIT_ASSERT_EQUAL(gif().currentFrame(), gif().getFrameCount() - 1);
    }
}
