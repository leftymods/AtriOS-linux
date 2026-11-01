#include "dhcpcd_dns_updater.h"

#include <yandex_io/libs/base/persistent_file.h>
#include <yandex_io/libs/subprocess/subprocess.h>

namespace quasar::dns {

    DhcpcdDnsUpdater::DhcpcdDnsUpdater(const std::string& resolvConfHeadPath)
        : resolvConfHead_(resolvConfHeadPath)
    {
    }
    void DhcpcdDnsUpdater::setCustomServers(const std::vector<TIpv6Address>& servers) {
        Y_VERIFY(!servers.empty());
        std::stringstream ss;
        ss << "# Quasar Custom DNS servers start\n";
        for (const auto& ip : servers) {
            bool ok{false};
            const auto ipStr = ip.ToString(&ok);
            if (ok) {
                ss << "nameserver " << ipStr << '\n';
            }
        }
        ss << "# Quasar Custom DNS servers end\n";

        {
            TransactionFile file(resolvConfHead_.GetPath());
            file.write(ss.str());
            file.commit();
        }
        runSystemCommand("dhcpcd", {"-n"});
    }

    void DhcpcdDnsUpdater::removeCustomServers() {
        resolvConfHead_.ForceDelete();
        runSystemCommand("dhcpcd", {"-n"});
    }

} // namespace quasar::dns
