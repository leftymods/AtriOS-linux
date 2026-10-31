#include <smart_devices/platforms/yandexstation_2/platformd/metrics_collector_impl.h>
#include <smart_devices/platforms/yandexstation_2/device/device.h>
#include <smart_devices/platforms/yandexstation_2/platformd/platformd.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/led_devices_impl.h>

#include <yandex_io/libs/android_support/opensles/init.h>
#include <yandex_io/libs/dispatch_main/dispatcher.h>
#include <yandex_io/libs/keymaster_proxy_app/keymaster_app.h>
#include <yandex_io/libs/quasar_client/quasar_client.h>
#include <yandex_io/modules/leds/led_util/led_util.h>

#include <yandex_io/scaffolding/base/entry.h>
#include <yandex_io/scaffolding/base/utils.h>
#include <yandex_io/scaffolding/calld/calld.h>
#include <yandex_io/scaffolding/fluent-bitd/fluent_bitd.h>
#include <yandex_io/scaffolding/glagold/glagold.h>
#include <yandex_io/scaffolding/mediad/mediad.h>
#include <yandex_io/scaffolding/telemetry/client/telemetry.h>
#include <yandex_io/scaffolding/telemetry/telemetry.h>
#include <yandex_io/scaffolding/updatesd/updatesd.h>
#include <yandex_io/scaffolding/yiod/yiod.h>

#include <yandex_io/tests/testlib/null_sdk/null_sdk_interface.h>

using namespace quasar;

int RunLedUtil(int argc, char** argv) {
    auto params = getQuasarCallParams(argc, argv);
    const auto& maindConfig = params.device()->configuration()->getServiceConfig("maind");
    std::unique_ptr<LedDevices> ledDevices = std::make_unique<LedDevicesImpl>(
        params.device()->deviceId(),
        quasar::getString(maindConfig, "aliceGifLedAnimationsPath"),
        std::make_shared<ClockDisplayStateHolder>(TFsPath(getString(maindConfig, "clockDisplayStatePath")), std::make_shared<YandexIO::NullSDKInterface>()));
    return led_util(argc, argv, std::move(ledDevices));
}

int main(int argc, char** argv) {
    android_support::initializeOpenSLES();

    RunDispatchMainCustom(argc, argv, {
                                          {"quasar_client", RunQuasarClient},
                                          {"led_util", RunLedUtil},
                                          {"keymaster_proxy_app", RunKeymasterProxyApp},
                                      });

    auto params = getQuasarCallParams(argc, argv);

    return callDaemons(params, {
                                   {"--yiod", yiod},
                                   {"--updatesd", updatesd},
                                   {"--mediad", mediad},
                                   {"--glagold", glagold},
                                   {"--yandexstation_2", platformd},
                                   {"--telemetry", telemetry<MetricsCollectorImpl>},
                                   {"--fluent-bitd", fluent_bitd},
                                   {"--calld", calld},
                               });
}
