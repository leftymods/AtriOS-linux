#include "wifid.h"

#include <yandex_io/scaffolding/base/utils.h>

#include <yandex_io/interfaces/device_state/connector/device_state_provider.h>
#include <yandex_io/interfaces/user_config/connector/user_config_provider.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/defines.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/logging_handler/logging_handler.h>
#include <yandex_io/libs/minidump/quasar_minidump.h>
#include <yandex_io/libs/telemetry/telemetry.h>
#include <yandex_io/libs/telemetry_handler/telemetry_handler.h>
#include <yandex_io/libs/terminate_waiter/terminate_waiter.h>
#include <yandex_io/libs/wpa_cli/wpa_client_supplicant.h>
#include <yandex_io/services/networkd/network_service.h>
#include <yandex_io/services/wifid/wifi_endpoint.h>
#include <yandex_io/services/wifid/wifi_manager.h>
#include <yandex_io/services/wifid/wifi_utils.h>

YIO_DEFINE_LOG_MODULE("wifi");

using namespace quasar;

namespace {

    class WifiService: public QuasarService {
    public:
        WifiService(std::shared_ptr<YandexIO::IDevice> device, std::shared_ptr<quasar::ipc::MixedIpcFactory> ipcFactory)
            : device_(std::move(device))
            , ipcFactory_(std::move(ipcFactory))
        {
        }

        std::string getServiceName() const override {
            return "wifid";
        }
        void start() override {
            const auto& config = device_->configuration()->getServiceConfig(WifiEndpoint::SERVICE_NAME);
            wpaClient_ = std::make_unique<WpaClientSupplicant>(tryGetString(config, "wpaCtrlSocketDir", "/var/run/wpa_supplicant"));
            wifiManager_ = std::make_unique<WifiManager>(device_, *wpaClient_);
            auto deviceStateProvider = std::make_shared<DeviceStateProvider>(ipcFactory_);
            auto userConfigProvider = std::make_shared<UserConfigProvider>(ipcFactory_);
            wifiEndpoint_ = std::make_unique<WifiEndpoint>(device_, ipcFactory_, deviceStateProvider, userConfigProvider, *wifiManager_);
        };

    private:
        const std::shared_ptr<YandexIO::IDevice> device_;
        const std::shared_ptr<quasar::ipc::MixedIpcFactory> ipcFactory_;
        std::unique_ptr<WpaClientSupplicant> wpaClient_;
        std::unique_ptr<WifiManager> wifiManager_;
        std::unique_ptr<WifiEndpoint> wifiEndpoint_;
    };

} // namespace

int wifid(QuasarCallParams& params)
{
    TerminateWaiter waiter;

    const auto device = params.device();
    const auto ipcFactory = params.mixedIpcFactory();

    ipcFactory->setProcessName("wifid");
    sendDaemonStartMetric("wifid", device);

    LoggingHandler loggingHandler;
    loggingHandler.init(ipcFactory, WifiEndpoint::SERVICE_NAME, device->configuration()->getFullConfig(), device->telemetry());

    TelemetryHandler telemetryHandler;
    telemetryHandler.init(device->telemetry(), ipcFactory);

    YIO_LOG_INFO("Starting service wifid version " << device->softwareVersion());

    const auto& config = device->configuration()->getServiceConfig(WifiEndpoint::SERVICE_NAME);
    QuasarMinidump::getInstance().init(WifiEndpoint::SERVICE_NAME, config, device->telemetry(), device->softwareVersion());

    WifiService wifiService(device, ipcFactory);
    wifiService.start();

    waiter.wait();

    return 0;
}

void addWifiServices(quasar::YandexIOEndpoint& yandexIOEndpoint, QuasarCallParams& params) {
    const auto device = params.device();
    const auto ipcFactory = params.mixedIpcFactory();

    yandexIOEndpoint.addService<WifiService>(device, ipcFactory);
}
