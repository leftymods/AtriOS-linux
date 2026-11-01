#include "runtime_dns_config.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/base/persistent_file.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <util/system/yassert.h>
#include <util/folder/path.h>

#include <string_view>
#include <sstream>

YIO_DEFINE_LOG_MODULE("dns");

namespace {

    constexpr const char* DNS_CONFIG_NAME = "dns";

    std::vector<TIpv6Address> getDnsList(const Json::Value& serversJson) {
        if (!serversJson.isArray()) {
            return {};
        }
        std::vector<TIpv6Address> servers;
        servers.reserve(serversJson.size());
        for (Json::ArrayIndex i = 0; i < serversJson.size(); ++i) {
            if (serversJson[i].isString()) {
                const TString ipStr = serversJson[i].asString();
                bool ok{false};
                const auto ip = TIpv6Address::FromString(ipStr, ok);
                if (ok) {
                    servers.push_back(ip);
                } else {
                    YIO_LOG_ERROR_EVENT("RuntimeDnsConfig.InvalidIp", "Invalid input dns server ip: " << ipStr);
                }
            }
        }
        return servers;
    }

} // namespace

namespace quasar::dns {

    RuntimeDnsConfig::RuntimeDnsConfig(std::shared_ptr<IDnsUpdater> dnsUpdater,
                                       std::shared_ptr<IDnsHealthChecker> healthChecker,
                                       std::shared_ptr<ICallbackQueue> worker,
                                       std::shared_ptr<YandexIO::ITelemetry> telemetry,
                                       std::chrono::milliseconds healthCheckPeriod,
                                       std::string backupFileName)
        : dnsUpdater_(std::move(dnsUpdater))
        , healthChecker_(std::move(healthChecker))
        , backupFileName_(std::move(backupFileName))
        , defaultHealthCheckPeriod_(healthCheckPeriod)
        , healthCheckPeriod_(defaultHealthCheckPeriod_)
        , telemetry_(std::move(telemetry))
        , worker_(std::move(worker))
        , healthCheckCallback_(worker_, quasar::UniqueCallback::ReplaceType::INSERT_BACK)
    {
        Y_VERIFY(dnsUpdater_);
        Y_VERIFY(healthChecker_);
        Y_VERIFY(worker_);
        Y_VERIFY(!backupFileName_.empty());

        const std::set<std::string> watchedConfigs = {DNS_CONFIG_NAME};
        configObserver_ = std::make_shared<YandexIO::CallbackBackendConfigObserver>(watchedConfigs, makeSafeCallback(
                                                                                                        [this](std::string_view /* configName */, const Json::Value& json) {
                                                                                                            handleBackendConfig(json);
                                                                                                        }, lifetime_, worker_));

        initFromBackupFile();
    }

    RuntimeDnsConfig::~RuntimeDnsConfig() {
        lifetime_.die();
    }

    void RuntimeDnsConfig::handleBackendConfig(const Json::Value& json) {
        Y_ENSURE_THREAD(worker_);

        if (dnsSettings_.has_value() && *dnsSettings_ == json) {
            return;
        }
        dnsSettings_ = json;
        persistState(std::nullopt);

        // update health check period
        if (const auto periodSec = tryGetInt(json, "healthCheckPeriodSec", -1); periodSec > 0) {
            healthCheckPeriod_ = std::chrono::seconds(periodSec);
        } else {
            healthCheckPeriod_ = defaultHealthCheckPeriod_;
        }
        // update health check timeout
        if (const auto timeoutSec = tryGetInt(json, "healthCheckTimeoutSec", -1); timeoutSec > 0) {
            healthChecker_->setSuccessTimeout(std::chrono::seconds(timeoutSec));
        } else {
            healthChecker_->setSuccessTimeout(std::nullopt);
        }

        const auto customDnsEnabled = tryGetBool(json, "customDnsEnabled", false);
        if (customDnsEnabled) {
            const auto servers = getDnsList(json["servers"]);
            if (!servers.empty()) {
                YIO_LOG_INFO("Set custom dns servers: " << jsonToString(json["servers"]));
                {
                    Json::Value payload;
                    payload["servers"] = json["servers"];
                    telemetry_->reportEvent("dnsSetCustomServers", jsonToString(payload));
                }
                dnsUpdater_->setCustomServers(servers);
                healthChecker_->start(); // healthchecker will stat collect data
                scheduleHealthCheck();
            } else {
                telemetry_->reportEvent("dnsRestoreDefaultServers");
                YIO_LOG_INFO("Empty dns server lists. Restore default settings");
                dnsUpdater_->removeCustomServers();
                healthChecker_->reset();
            }
        } else {
            YIO_LOG_INFO("Restore default DNS settings");
            telemetry_->reportEvent("dnsRestoreDefaultServers");
            dnsUpdater_->removeCustomServers();
            healthChecker_->reset();
        }
    }

    void RuntimeDnsConfig::scheduleHealthCheck() {
        healthCheckCallback_.executeDelayed([this]() {
            const auto status = healthChecker_->healthCheck();
            switch (status) {
                case IDnsHealthChecker::Status::OK: {
                    YIO_LOG_INFO("All dns healthchecks passed. Keep custom dns");
                    healthChecker_->reset(); // stop collect data
                    telemetry_->reportEvent("dnsHealthCheckOk");
                    persistState(true);
                    break;
                }
                case IDnsHealthChecker::Status::POSTPONE: {
                    YIO_LOG_INFO("Dns health check postponed");
                    scheduleHealthCheck();
                    break;
                }
                case IDnsHealthChecker::Status::FAILED: {
                    YIO_LOG_ERROR_EVENT("RuntimeDnsConfig.HealthCheckFailed", "Health check failed. Restore default dns");
                    dnsUpdater_->removeCustomServers();
                    healthChecker_->reset();
                    telemetry_->reportEvent("dnsHealthCheckFailed");
                    persistState(false);
                    break;
                }
            }
        }, healthCheckPeriod_, lifetime_);
    }

    void RuntimeDnsConfig::subscribeToSDK(YandexIO::SDKInterface& sdk) {
        sdk.addBackendConfigObserver(configObserver_);
        sdk.subscribeToSystemConfig(DNS_CONFIG_NAME);
    }

    void RuntimeDnsConfig::persistState(std::optional<bool> healthCheckPassed) const {
        Json::Value json;
        if (dnsSettings_.has_value()) {
            json["runtimeConfig"] = *dnsSettings_;
        }
        if (healthCheckPassed.has_value()) {
            json["healthCheckPassed"] = *healthCheckPassed;
        }

        TransactionFile file(backupFileName_);
        file.write(jsonToString(json));
        file.commit();
    }

    void RuntimeDnsConfig::initFromBackupFile() {
        if (!TFsPath(backupFileName_).Exists()) {
            return;
        }
        const auto optJson = tryParseJson(getFileContent(backupFileName_));
        if (!optJson.has_value()) {
            return;
        }
        const auto& json = *optJson;
        if (!json.isMember("runtimeConfig")) {
            return;
        }

        // update runtime variables through worker thread
        worker_->add([this, json]() {
            std::optional<bool> healthCheckPassed;
            if (json.isMember("healthCheckPassed") && json["healthCheckPassed"].isBool()) {
                healthCheckPassed = json["healthCheckPassed"].asBool();
            }

            /**
         * Either way if dnsHealthCheck passed or not -> there is no need to update
         * dns and make any health checks.
         * DNS settings should be persistent, so there is no need to
         * call dnsUpdater implicitly
         */
            if (healthCheckPassed.has_value()) {
                dnsSettings_ = json["runtimeConfig"];
                return;
            }
            // handle config from scratch if healthCheck status is not persisted
            handleBackendConfig(json["runtimeConfig"]);
        });
    }

} // namespace quasar::dns
