#pragma once

#include <yandex_io/sdk/sdk_interface.h>
#include <yandex_io/sdk/sdk_state_observer.h>

#include <yandex_io/modules/backend_config_observer/callback_backend_config_observer.h>

#include <yandex_io/libs/telemetry/telemetry.h>
#include <yandex_io/libs/threading/i_callback_queue.h>
#include <yandex_io/libs/threading/unique_callback.h>
#include <yandex_io/libs/threading/lifetime.h>

#include <library/cpp/ipv6_address/ipv6_address.h>

#include <json/json.h>

#include <chrono>
#include <string>
#include <memory>
#include <vector>
#include <optional>

namespace quasar::dns {

    class RuntimeDnsConfig {
    public:
        class IDnsUpdater;
        class IDnsHealthChecker;
        RuntimeDnsConfig(std::shared_ptr<IDnsUpdater> dnsUpdater,
                         std::shared_ptr<IDnsHealthChecker> healthChecker,
                         std::shared_ptr<ICallbackQueue> worker,
                         std::shared_ptr<YandexIO::ITelemetry> telemetry,
                         std::chrono::milliseconds healthCheckPeriod,
                         std::string backupFileName);
        ~RuntimeDnsConfig();
        void subscribeToSDK(YandexIO::SDKInterface& sdk);

    private:
        void handleBackendConfig(const Json::Value& json);
        void scheduleHealthCheck();

        void initFromBackupFile();
        void persistState(std::optional<bool> healthCheckPassed) const;

        const std::shared_ptr<IDnsUpdater> dnsUpdater_;
        const std::shared_ptr<IDnsHealthChecker> healthChecker_;
        std::optional<Json::Value> dnsSettings_;

        const std::string backupFileName_;
        const std::chrono::milliseconds defaultHealthCheckPeriod_;
        std::chrono::milliseconds healthCheckPeriod_;
        const std::shared_ptr<YandexIO::ITelemetry> telemetry_;
        const std::shared_ptr<ICallbackQueue> worker_;
        UniqueCallback healthCheckCallback_;
        Lifetime lifetime_;
        std::shared_ptr<YandexIO::CallbackBackendConfigObserver> configObserver_;
    };

    class RuntimeDnsConfig::IDnsUpdater {
    public:
        virtual ~IDnsUpdater() = default;
        /**
     * @brief Implementation should add custrom dns servers to defaults
     */
        virtual void setCustomServers(const std::vector<TIpv6Address>& servers) = 0;
        /**
     * @brief Remove custom dns servers
     */
        virtual void removeCustomServers() = 0;
    };

    class RuntimeDnsConfig::IDnsHealthChecker {
    public:
        virtual ~IDnsHealthChecker() = default;
        enum class Status {
            OK,
            FAILED,
            POSTPONE, // health check can't be performed at this moment. postpone it
        };
        virtual void setSuccessTimeout(std::optional<std::chrono::milliseconds> timeout) = 0;
        virtual Status healthCheck() const = 0;
        virtual void start() = 0;
        virtual void reset() = 0;
    };

} // namespace quasar::dns
