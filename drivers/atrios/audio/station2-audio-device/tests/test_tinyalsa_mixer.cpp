#include <smart_devices/platforms/yandexstation_2/platformd/audio_device/tinyalsa_mixer.h>

#include <library/cpp/testing/unittest/registar.h>

namespace quasar {

    Y_UNIT_TEST_SUITE(TestTinyAlsaMixer) {
        Y_UNIT_TEST(TestMixerDoesNotThrowWithNoAlsa) {
            TinyAlsaMixer mixer;
            mixer.enableHardwareSync();
            mixer.disableHardwareSync();
        }
    }

} // namespace quasar
