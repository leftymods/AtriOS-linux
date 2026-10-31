#pragma once

#include "runtime_dns_config.h"

#include <util/folder/path.h>

namespace quasar::dns {

    class DhcpcdDnsUpdater: public RuntimeDnsConfig::IDnsUpdater {
    public:
        explicit DhcpcdDnsUpdater(const std::string& resolvConfHeadPath);
        /**
     * @brief Write DNS servers to resolve.conf.head file and update resolve.conf
     *        via reconfiguring dhcpcd
     */
        void setCustomServers(const std::vector<TIpv6Address>& servers) override;

        /**
     * @brief remove custom resolve.conf.head and update resolve.conf via reconfiguring dhcpcd
     */
        void removeCustomServers() override;

    private:
        TFsPath resolvConfHead_;
    };

} // namespace quasar::dns
