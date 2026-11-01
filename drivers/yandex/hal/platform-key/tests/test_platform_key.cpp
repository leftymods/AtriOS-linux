#include <smart_devices/libs/platform_key/platform_key.h>

#include <library/cpp/testing/unittest/registar.h>

#include <yandex_io/libs/cryptography/cryptography.h>

using namespace quasar;

Y_UNIT_TEST_SUITE(PlatformKey) {

    Y_UNIT_TEST(testVendorKeyCorrect) {
        Cryptography cryptography;
        cryptography.setPrivateKey(getPlatformKey());
    }

}
