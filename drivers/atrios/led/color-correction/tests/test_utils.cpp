#include <smart_devices/libs/color_correction/utils.h>

#include <library/cpp/testing/unittest/registar.h>

using namespace quasar;

template <>
void Out<std::tuple<float, float, float>>(IOutputStream& o, const std::tuple<float, float, float>& value) {
    o << "(" << std::get<0>(value) << ", " << std::get<1>(value) << ", " << std::get<2>(value) << ")";
}

Y_UNIT_TEST_SUITE(TestUtils) {

    Y_UNIT_TEST(testRgbToHsv) {
        UNIT_ASSERT_VALUES_EQUAL(
            rgbToHsv(1.0f, 1.0f, 1.0f),
            std::make_tuple(0.0f, 0.0f, 1.0f));

        UNIT_ASSERT_VALUES_EQUAL(
            rgbToHsv(0.0f, 0.0f, 0.0f),
            std::make_tuple(0.0f, 0.0f, 0.0f));

        UNIT_ASSERT_VALUES_EQUAL(
            rgbToHsv(1.0f, 0.0f, 0.0f),
            std::make_tuple(0.0f, 1.0f, 1.0f));

        UNIT_ASSERT_VALUES_EQUAL(
            rgbToHsv(0.0f, 1.0f, 0.0f),
            std::make_tuple(120.0f, 1.0f, 1.0f));

        UNIT_ASSERT_VALUES_EQUAL(
            rgbToHsv(0.0f, 0.0f, 1.0f),
            std::make_tuple(240.0f, 1.0f, 1.0f));

        UNIT_ASSERT_VALUES_EQUAL(
            rgbToHsv(0.5f, 0.5f, 0.0f),
            std::make_tuple(60.0f, 1.0f, 0.5f));
    }

    Y_UNIT_TEST(testHsvToRgb) {
        UNIT_ASSERT_VALUES_EQUAL(
            hsvToRgb(0.0f, 0.0f, 0.0f),
            std::make_tuple(0.0f, 0.0f, 0.0f));

        UNIT_ASSERT_VALUES_EQUAL(
            hsvToRgb(0.0f, 0.0f, 1.0f),
            std::make_tuple(1.0f, 1.0f, 1.0f));

        UNIT_ASSERT_VALUES_EQUAL(
            hsvToRgb(0.0f, 1.0f, 1.0f),
            std::make_tuple(1.0f, 0.0f, 0.0f));

        UNIT_ASSERT_VALUES_EQUAL(
            hsvToRgb(120.0f, 1.0f, 1.0f),
            std::make_tuple(0.0f, 1.0f, 0.0f));

        UNIT_ASSERT_VALUES_EQUAL(
            hsvToRgb(240.0f, 1.0f, 1.0f),
            std::make_tuple(0.0f, 0.0f, 1.0f));

        UNIT_ASSERT_VALUES_EQUAL(
            hsvToRgb(180.0f, 1.0f, 0.5f),
            std::make_tuple(0.0f, 0.5f, 0.5f));
    }

}
