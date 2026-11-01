#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>

#include <contrib/libs/cxxsupp/libcxx/include/fstream>

#include <library/cpp/testing/unittest/registar.h>

#include <yandex_io/libs/logging/logging.h>

#include <functional>
#include <stdexcept>
#include <vector>

namespace quasar {

    auto proceed = [](const std::shared_ptr<Animation>& animation) { animation->updateTime(animation->getEndOfFrameTimePoint()); };

    class NullYandexStationFrontalController: public YandexStationFrontalController {
    public:
        NullYandexStationFrontalController() {
        }

        mutable bool drawn = false; // for testing

        void drawBuffer(const Frame& /* frame */) override {
            drawn = true;
        }
    };

    void writeTempGif(const char* filename, unsigned char gif[], const int size) {
        std::ofstream myfile;
        myfile.open(filename, std::ios::binary);
        myfile.write(reinterpret_cast<char*>(gif), size);
        myfile.close();
    }

    Y_UNIT_TEST_SUITE(testGifLoad) {
        unsigned char singlePixelGif[] = {
            0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x21, 0xf9, 0x04, 0x01, 0x0a, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3b};
        const int singlePixelGifSize = 43;

        Y_UNIT_TEST(testOneFrame) {
            Animation::TimePoint animationStart = std::chrono::steady_clock::now(); // could be anything

            auto nullYandexStationFrontalController = std::make_shared<NullYandexStationFrontalController>();
            const char* filename = "./singlePixelGif.gif";

            writeTempGif(filename, singlePixelGif, singlePixelGifSize);
            auto gifAnimation = GifAnimation::readGif(filename, nullYandexStationFrontalController);
            gifAnimation->startAnimationFrom(animationStart);

            gifAnimation->drawCurrentFrame();
            proceed(gifAnimation);

            UNIT_ASSERT(nullYandexStationFrontalController->drawn);
            UNIT_ASSERT(gifAnimation->finished());
        }

        unsigned char twoFrameGif[] = {
            0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x21, 0xF9, 0x04, 0x01, 0x0A, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x21, 0xF9, 0x04, 0x01, 0x0A, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3B};
        const int twoFrameGifSize = 66;

        Y_UNIT_TEST(testTwoFrames) {
            Animation::TimePoint animationStart = std::chrono::steady_clock::now(); // could be anything
            auto nullYandexStationFrontalController = std::make_shared<NullYandexStationFrontalController>();

            const char* filename = "./twoFrameGif.gif";
            writeTempGif(filename, twoFrameGif, twoFrameGifSize);

            auto gifAnimation = GifAnimation::readGif(filename, nullYandexStationFrontalController);
            gifAnimation->startAnimationFrom(animationStart);
            proceed(gifAnimation);
            gifAnimation->drawCurrentFrame();
            proceed(gifAnimation);
            UNIT_ASSERT(nullYandexStationFrontalController->drawn);
            UNIT_ASSERT(gifAnimation->finished());
        }

    } // Y_UNIT_TEST_SUITE(testGifLoad)

} // namespace quasar
