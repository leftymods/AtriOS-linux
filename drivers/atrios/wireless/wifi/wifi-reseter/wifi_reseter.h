#pragma once

#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/telemetry/telemetry.h>
#include <yandex_io/sdk/sdk_interface.h>

namespace quasar {
    // this class make decision about reseting wifi by watching varios system counters and internet access
    class WifiReseter {
    public:
        virtual void resetWifiByButton() = 0;
        virtual ~WifiReseter() = default;
    };

    // this class should disable/enable wifi in system (eg in android system via sending message to WifiManger)
    class IWifiSwitcher {
    public:
        virtual void turnWifiOff() = 0;
        virtual void turnWifiOn() = 0;
        virtual bool needPciRemove() = 0;
        virtual void oldResetViaReconfigure() = 0;
        virtual ~IWifiSwitcher() = default;
    };

    std::shared_ptr<IWifiSwitcher> makeStationWifiSwitcher(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory);
    std::shared_ptr<IWifiSwitcher> makeMinisWifiSwitcher(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory);

    // Wifi reseter subscribe to system config "wifi_reseter" to monitor new settings
    std::shared_ptr<WifiReseter> makeWifiReseter(std::shared_ptr<YandexIO::SDKInterface> sdk,
                                                 std::shared_ptr<YandexIO::ITelemetry> telemetry,
                                                 std::shared_ptr<IWifiSwitcher> switcher);
} // namespace quasar
