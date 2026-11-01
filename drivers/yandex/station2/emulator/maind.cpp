#include <smart_devices/platforms/yandexstation_2/platformd/metrics_collector_impl.h>
#include <smart_devices/platforms/yandexstation_2/platformd/platformd.h>
#include <smart_devices/platforms/yandexstation_2/device/device.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/dispatch_main/dispatcher.h>
#include <yandex_io/libs/keymaster_proxy_app/keymaster_app.h>
#include <yandex_io/libs/quasar_client/quasar_client.h>
#include <yandex_io/scaffolding/base/entry.h>
#include <yandex_io/scaffolding/glagold/glagold.h>
#include <yandex_io/scaffolding/mediad/mediad.h>
#include <yandex_io/scaffolding/telemetry/telemetry.h>
#include <yandex_io/scaffolding/updatesd/updatesd.h>
#include <yandex_io/scaffolding/yiod/yiod.h>

using namespace quasar;

int main(int argc, char** argv) {
    RunDispatchMainCustom(argc, argv, {{"quasar_client", RunQuasarClient}, {"keymaster_proxy_app", RunKeymasterProxyApp}});

    auto params = getQuasarCallParams(argc, argv);

    return callDaemons(params, {{"--yiod", yiod},
                                {"--updatesd", updatesd},
                                {"--mediad", mediad},
                                {"--glagold", glagold},
                                {"--yandexstation_2", platformd},
                                {"--telemetry", telemetry<MetricsCollectorImpl>}});
}
