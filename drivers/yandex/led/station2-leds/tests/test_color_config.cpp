#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/frontal_animation/color_config.h>

#include <library/cpp/testing/unittest/registar.h>

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

namespace {

    using Frame = std::vector<BGR24>;

    Y_UNIT_TEST_SUITE(colorConfig) {

        Y_UNIT_TEST(testReadConfig) {
            Json::Value groupConfig;
            groupConfig["config"] = Json::Value();

            Json::Value config;
            config["black"]["gamma"] = 2.05;
            config["white"]["gamma"] = 1.0;
            config["default"]["gamma"] = 2.05;

            ColorConfig colorConfig;
            colorConfig.updateConfig(config, ColorConfig::DeviceExteriorColor::BLACK);

            Json::Value config2;
            config2["black"] = "";
            config2["white"]["gamma"] = Json::Value();
            config2["default"]["gamma"] = 2.05;

            colorConfig.updateConfig(config2, ColorConfig::DeviceExteriorColor::BLACK);

        } // Y_UNIT_TEST(testReadConfig)

        Y_UNIT_TEST(testColorCorrection) {
            Json::Value groupConfig;
            groupConfig["config"] = Json::Value();

            Json::Value config;
            config["black"]["gamma"] = 2.05;
            config["white"]["gamma"] = 1.0;
            config["default"]["gamma"] = 2.05;

            ColorConfig colorConfig;
            colorConfig.updateConfig(config, ColorConfig::DeviceExteriorColor::BLACK);
            quasar::Gif gif{quasar::GifFrame{{{1, 1}, Frame{BGR24{50, 50, 50}}}, 10}};
            quasar::Gif result = colorConfig.applyCorrection(gif, 1.0);
            UNIT_ASSERT_EQUAL(result[0].data[0].b, 9);

        } // Y_UNIT_TEST(testColorCorrection)

        Y_UNIT_TEST(testGetColor) {
            auto color = ColorConfig::getDeviceColor("XK0000000000000000200000f9c2d34b");
            UNIT_ASSERT_EQUAL(color, ColorConfig::DeviceExteriorColor::BLACK);

        } // Y_UNIT_TEST(testGetColor)

    }

} // namespace
