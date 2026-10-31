#include "installer.h"

#include "dns_health_checker.h"
#include "dhcpcd_dns_updater.h"

#include <yandex_io/libs/base/named_callback_queue.h>

namespace quasar::dns {

    std::shared_ptr<RuntimeDnsConfig> installDhcpcdRuntimeDnsConfig(YandexIO::SDKInterface& sdk,
                                                                    std::shared_ptr<YandexIO::ITelemetry> telemetry,
                                                                    const std::string& backupFilePath,
                                                                    const std::string& resolvConfHeadPath) {
        // todo: adjust setup settings
        auto worker = std::make_shared<NamedCallbackQueue>("dns");
        auto dnsHealthChecker = DnsHealthChecker::create(worker, std::chrono::minutes(20), std::chrono::seconds(10));
        dnsHealthChecker->subscribeToSDK(sdk);
        auto dhcpcdDnsUpdater = std::make_shared<DhcpcdDnsUpdater>(resolvConfHeadPath);
        auto runtimeDnsConfig = std::make_shared<RuntimeDnsConfig>(std::move(dhcpcdDnsUpdater),
                                                                   std::move(dnsHealthChecker),
                                                                   std::move(worker),
                                                                   std::move(telemetry),
                                                                   std::chrono::minutes(5),
                                                                   backupFilePath);
        runtimeDnsConfig->subscribeToSDK(sdk);
        return runtimeDnsConfig;
    }

} // namespace quasar::dns
