#include <yandex_io/libs/audio/alsa/alsa_error.h>

#include <library/cpp/testing/unittest/registar.h>

using namespace quasar;

Y_UNIT_TEST_SUITE(AlsaUtilsErrorTest) {
    Y_UNIT_TEST(TestEmptyStringOnUnkonwnError) {
        UNIT_ASSERT_VALUES_EQUAL(alsaErrorTextMessage(0), "0:");
    }
}
