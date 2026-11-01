#include <smart_devices/libs/brightness_control/brightness_config.h>

#include <library/cpp/testing/unittest/registar.h>

#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

namespace {

    const BrightnessConfig::LinearCurve BRIGHTNESS_CURVE = {
        {0.0f, 3},
        {1.0f, 30},
        {10.0f, 100},
        {50.0f, 150},
        {120.0f, 200},
        {256.0f, 255}};

    constexpr uint32_t MIN_FIXED_BRIGHTNESS = 0;
    constexpr uint32_t MAX_FIXED_BRIGHTNESS = 255;

    Y_UNIT_TEST_SUITE(brightnessConfig) {

        Y_UNIT_TEST(testDefaultLevels) {
            BrightnessConfig brightnessConfig(BRIGHTNESS_CURVE, MIN_FIXED_BRIGHTNESS, MAX_FIXED_BRIGHTNESS);
            uint32_t level30 = brightnessConfig.getLevel(30.0f);
            UNIT_ASSERT_EQUAL(level30, 125);

            uint32_t level10 = brightnessConfig.getLevel(10.0f);
            UNIT_ASSERT_EQUAL(level10, 100);

            uint32_t level40 = brightnessConfig.getLevel(40.0f);
            UNIT_ASSERT_EQUAL(level40, 137);

            uint32_t level0 = brightnessConfig.getLevel(0.0f);
            UNIT_ASSERT_EQUAL(level0, 3);

            uint32_t level1 = brightnessConfig.getLevel(1.0f);
            UNIT_ASSERT_EQUAL(level1, 30);

            uint32_t levelSun = brightnessConfig.getLevel(40000.0f);
            UNIT_ASSERT_EQUAL(levelSun, 255);

            uint32_t levelUnreal = brightnessConfig.getLevel(-50.0f);
            UNIT_ASSERT_EQUAL(levelUnreal, 3);

            uint32_t level50 = brightnessConfig.getLevel(50.0f);
            UNIT_ASSERT_EQUAL(level50, 150);

            uint32_t level120 = brightnessConfig.getLevel(120.0f);
            UNIT_ASSERT_EQUAL(level120, 200);

            uint32_t level256 = brightnessConfig.getLevel(256.0f);
            UNIT_ASSERT_EQUAL(level256, 255);
        } // Y_UNIT_TEST(testDefaultLevels)

    }

} // namespace
