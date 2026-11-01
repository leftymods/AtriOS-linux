#include <smart_devices/libs/zigbee/utils.h>

#include <library/cpp/testing/gmock_in_unittest/gmock.h>
#include <library/cpp/testing/unittest/registar.h>

namespace {
    Y_UNIT_TEST_SUITE(UtilsTest) {
        Y_UNIT_TEST(TestEui64FromString) {
            zigbee::EUI64 expected = {0xA8, 0x8E, 0x19, 0x00, 0x10, 0x44, 0xEF, 0x54};
            auto res = zigbee::eui64FromString("A88E19001044EF54");
            UNIT_ASSERT(res);
            EXPECT_THAT(*res, ::testing::ContainerEq(expected));

            expected = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            res = zigbee::eui64FromString("FFFFFFFFFFFFFFFF");
            UNIT_ASSERT(res);
            EXPECT_THAT(*res, ::testing::ContainerEq(expected));

            expected = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            res = zigbee::eui64FromString("0000000000000000");
            UNIT_ASSERT(res);
            EXPECT_THAT(*res, ::testing::ContainerEq(expected));

            res = zigbee::eui64FromString("iot_zigbee_F000000000000000");
            UNIT_ASSERT(!res);

            res = zigbee::eui64FromString("A88E19001044EF54");
            auto res2 = zigbee::eui64FromString("a88E19001044eF54");
            UNIT_ASSERT(res);
            UNIT_ASSERT(res2);
            EXPECT_THAT(*res, ::testing::ContainerEq(*res2));
        }
    }
} // namespace
