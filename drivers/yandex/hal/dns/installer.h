#pragma once

#include "runtime_dns_config.h"

#include <yandex_io/libs/telemetry/telemetry.h>

#include <memory>
#include <string>

namespace quasar::dns {

    std::shared_ptr<RuntimeDnsConfig> installDhcpcdRuntimeDnsConfig(YandexIO::SDKInterface& sdk,
                                                                    std::shared_ptr<YandexIO::ITelemetry> telemetry,
                                                                    const std::string& backupFilePath,
                                                                    const std::string& resolvConfHeadPath);

} // namespace quasar::dns
