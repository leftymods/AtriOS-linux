#include <smart_devices/libs/zigbee/zigbee.h>

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <library/cpp/testing/unittest/registar.h>

using namespace zigbee;

Y_UNIT_TEST_SUITE(TestZigbee) {

    Y_UNIT_TEST(testNetworkParametersIsValid) {
        NetworkParameters parameters = {};
        UNIT_ASSERT(!parameters.isValid());

        parameters.channel = 15;
        parameters.extendedPanId[0] = 0xff;
        UNIT_ASSERT(parameters.isValid());

        parameters.channel = 239;
        UNIT_ASSERT(!parameters.isValid());
    }

}
