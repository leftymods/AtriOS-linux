#pragma once

#include "runtime_dns_config.h"

#include <yandex_io/sdk/sdk_interface.h>
#include <yandex_io/sdk/backend_config_observer.h>
#include <yandex_io/sdk/sdk_state_observer.h>

#include <optional>
#include <memory>
#include <chrono>

namespace quasar::dns {

    /**
 * TODO: 1) Handle ethernet state for YandexStation-2
 */
    class DnsHealthChecker: public RuntimeDnsConfig::IDnsHealthChecker,
                            public YandexIO::SDKStateObserver,
                            public YandexIO::BackendConfigObserver,
                            public std::enable_shared_from_this<DnsHealthChecker> {
    public:
        [[nodiscard]] static std::shared_ptr<DnsHealthChecker> create(std::shared_ptr<ICallbackQueue> worker,
                                                                      std::chrono::milliseconds getSyncInfoSuccessTimeout,
                                                                      std::chrono::milliseconds firstGetSyncInfoTimeout);
        ~DnsHealthChecker();

        // sdk observers
        void onSDKState(const YandexIO::SDKState& state) override;
        void onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) override;
        void onBackendConfigHandleStatus(YandexIO::BackendConfigObserver::BackendConfigHandleStatus status) override;

        // health check api:
        Status healthCheck() const override;
        void start() override;
        void reset() override;
        void setSuccessTimeout(std::optional<std::chrono::milliseconds> timeout) override;

        void subscribeToSDK(YandexIO::SDKInterface& sdk);

    private:
        explicit DnsHealthChecker(std::shared_ptr<ICallbackQueue> worker,
                                  std::chrono::milliseconds getSyncInfoSuccessTimeout,
                                  std::chrono::milliseconds firstGetSyncInfoTimeout);

        std::optional<std::chrono::steady_clock::time_point> startTp_;
        std::optional<YandexIO::BackendConfigObserver::BackendConfigHandleStatus> getSyncInfoStatus_;
        std::optional<YandexIO::SDKState::WifiState> wifiState_;
        const std::chrono::milliseconds firstGetSyncInfoTimeout_;
        const std::chrono::milliseconds defaultGetSyncInfoTimeout_;
        std::chrono::milliseconds getSyncInfoTimeout_;
        const std::shared_ptr<ICallbackQueue> worker_;
        Lifetime lifetime_;
    };

} // namespace quasar::dns
