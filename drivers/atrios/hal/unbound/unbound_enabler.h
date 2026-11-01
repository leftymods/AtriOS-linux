#pragma once

#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/telemetry/telemetry.h>
#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/sdk/sdk_interface.h>

#include <vector>

/*! Class to switch systemwide resolve to local 'unbound' and back
 */

class UnboundEnabler: public YandexIO::BackendConfigObserver {
public:
    using NamesToIPs = std::map<std::string, std::vector<std::string>>;
    struct Config {
        std::chrono::seconds delayToCheck{60};
        std::vector<std::string> hostsToResolve;
        bool randomizeForwarders{false};
        std::optional<bool> forwardFirst;   // forward-first
        std::optional<bool> forwardNoCache; // forward-no-cache
        std::vector<std::string> forwarders;
        std::vector<std::string> serverParams;
        std::map<std::string, NamesToIPs> zones; // zone -> { host -> ip }

        bool operator==(const Config&) const = default;
    };

    class Reconfigurer {
    public:
        virtual void reconfigureNetwork() = 0;
        virtual ~Reconfigurer() = default;
    };

    using ReconfigurerPtr = std::shared_ptr<Reconfigurer>;
    UnboundEnabler(ReconfigurerPtr reconfigurer, std::shared_ptr<YandexIO::ITelemetry> telemetry);

    static bool parseConfig(const std::string& jsonConfigValue, Config& dst);
    static std::string buildUnboundConf(const Config& cfg);

private:
    void delayResolveCheck();
    void disableUnbound();
    void onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) override;
    void processConfig(const std::string& jsonConfigValue);
    bool configHeadChanged() const;

    bool previousAttemptFailed_{false};
    ReconfigurerPtr reconfigurer_;
    Config cfg_;
    std::shared_ptr<YandexIO::ITelemetry> telemetry_;
    quasar::NamedCallbackQueue callbackQueue_{"UnboundEnabler"};
};

UnboundEnabler::ReconfigurerPtr makeNetworkdReconfigurer(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory);

std::shared_ptr<UnboundEnabler> makeUnboundEnabler(YandexIO::SDKInterface& sdk, std::shared_ptr<YandexIO::ITelemetry> telemetry, UnboundEnabler::ReconfigurerPtr reconfigurer);
