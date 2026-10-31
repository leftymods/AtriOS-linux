#include "dns_health_checker.h"

#include <yandex_io/libs/logging/logging.h>

#include <util/system/yassert.h>

namespace quasar::dns {

    std::shared_ptr<DnsHealthChecker> DnsHealthChecker::create(std::shared_ptr<ICallbackQueue> worker,
                                                               std::chrono::milliseconds getSyncInfoSuccessTimeout,
                                                               std::chrono::milliseconds firstGetSyncInfoTimeout) {
        return std::shared_ptr<DnsHealthChecker>(new DnsHealthChecker(std::move(worker), getSyncInfoSuccessTimeout, firstGetSyncInfoTimeout));
    }

    // DnsHealthChecker
    DnsHealthChecker::DnsHealthChecker(std::shared_ptr<ICallbackQueue> worker,
                                       std::chrono::milliseconds getSyncInfoSuccessTimeout,
                                       std::chrono::milliseconds firstGetSyncInfoTimeout)
        : firstGetSyncInfoTimeout_(firstGetSyncInfoTimeout)
        , defaultGetSyncInfoTimeout_(getSyncInfoSuccessTimeout)
        , getSyncInfoTimeout_(defaultGetSyncInfoTimeout_)
        , worker_(std::move(worker))
    {
        Y_VERIFY(worker_);
    }

    DnsHealthChecker::~DnsHealthChecker() {
        lifetime_.die();
    }

    void DnsHealthChecker::onSDKState(const YandexIO::SDKState& state) {
        worker_->add([this, wifiState{state.wifiState}]() {
            wifiState_ = wifiState;
        }, lifetime_);
    }

    void DnsHealthChecker::onSystemConfig(const std::string& /* configName */, const std::string& /* jsonConfigValue */) {
        // no-op
    }

    void DnsHealthChecker::onBackendConfigHandleStatus(const YandexIO::BackendConfigObserver::BackendConfigHandleStatus status) {
        worker_->add([this, status]() {
            const auto now = std::chrono::steady_clock::now();
            if (startTp_.has_value()) {
                const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - *startTp_);
                // NOTE: this Timeout is used to simplify contract between syncd and this handler
                // We need to guarantee that healthcheck will use BackendConfigHandleStatus called
                // after dns settings changed
                if (diff >= firstGetSyncInfoTimeout_) {
                    getSyncInfoStatus_ = status;
                }
            }
        }, lifetime_);
    }

    DnsHealthChecker::Status DnsHealthChecker::healthCheck() const {
        using ConfigStatus = YandexIO::BackendConfigObserver::BackendConfigHandleStatus;

        Y_ENSURE_THREAD(worker_);
        Y_VERIFY(startTp_.has_value());

        // todo: handle ethernet for station-2
        if (!wifiState_.has_value()) {
            // should not happen
            return Status::POSTPONE;
        }
        if (!wifiState_->isWifiConnected) {
            YIO_LOG_WARN("Can't process dns healthcheck. Postpone it");
            // Can't check that internet is available without wifi
            // health check should be rescheduled
            return Status::POSTPONE;
        }
        // generate_204 should be always ok
        if (!wifiState_->isInternetReachable) {
            return Status::FAILED;
        }
        if (getSyncInfoStatus_.value_or(ConfigStatus::NETWORK_ERROR) == ConfigStatus::OK) {
            return Status::OK;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(now - *startTp_);
        // NOTE: This check will fail if health check will be called right after wifi reconnect
        //       Consider it good enough for now
        if (diff >= getSyncInfoTimeout_) {
            YIO_LOG_WARN("Dns health check failed: get_sync_info success timeout");
            return Status::FAILED;
        }
        YIO_LOG_INFO("Generate204 dns healthcheck passed. Schedule next healthcheck");
        return Status::POSTPONE;
    }

    void DnsHealthChecker::reset() {
        Y_ENSURE_THREAD(worker_);
        getSyncInfoStatus_.reset();
        startTp_.reset();
    }

    void DnsHealthChecker::start() {
        Y_ENSURE_THREAD(worker_);
        reset();
        startTp_ = std::chrono::steady_clock::now();
    }

    void DnsHealthChecker::setSuccessTimeout(std::optional<std::chrono::milliseconds> timeout) {
        getSyncInfoTimeout_ = timeout.value_or(defaultGetSyncInfoTimeout_);
    }

    void DnsHealthChecker::subscribeToSDK(YandexIO::SDKInterface& sdk) {
        const auto wthis = weak_from_this();
        sdk.addBackendConfigObserver(wthis);
        sdk.addSDKStateObserver(wthis);
    }

} //namespace quasar::dns
