#include "unbound_enabler.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/net/resolve.h>
#include <yandex_io/libs/base/persistent_file.h>
#include <util/random/shuffle.h>

#include <fstream>

namespace {
    constexpr const char* CONFIG_NAME = "unbound";
    constexpr const char* TRIGGER_PATH = "/data/unbound_enabled";
    constexpr const char* CUSTOM_HEAD_PATH = "/data/unbound.conf.head";

    void enableUnbound() {
        std::ofstream of(TRIGGER_PATH);
        of << time(nullptr); // existense of this file is a trigger. Content's ignored. Time is here just for debug purposes
        of.flush();
        of.close();
    }

    void disableUnboundTrigger() {
        unlink(TRIGGER_PATH);
    }

    bool currentlyEnabled() {
        return quasar::fileExists(TRIGGER_PATH);
    }

    void storeCustomHead(const std::string& content) {
        quasar::AtomicFile dst(CUSTOM_HEAD_PATH);
        dst.write(content);
    }

    std::optional<std::string> tryToResolve(const std::string& host) {
        auto resolves = quasar::resolve(host);

        std::array<char, 100> buf;
        if (!resolves.ipv4.empty()) {
            return inet_ntop(AF_INET, &resolves.ipv4.back().sin_addr, buf.data(), buf.size());
        }

        if (!resolves.ipv6.empty()) {
            return inet_ntop(AF_INET6, &resolves.ipv6.back().sin6_addr, buf.data(), buf.size());
        }

        return std::nullopt;
    }

    std::string yesOrNoParam(std::optional<bool> param, std::string name) {
        if (param) {
            name += ": ";
            name += *param ? "yes" : "no";
            name += '\n';
            return name;
        }
        return std::string();
    }

    UnboundEnabler::NamesToIPs convertNamesToIPs(const Json::Value& src) {
        UnboundEnabler::NamesToIPs result;
        if (src.isObject()) {
            const auto end = src.end();
            for (auto it = src.begin(); it != end; ++it) {
                std::vector<std::string> addrs;
                if (it->isArray()) {
                    addrs = quasar::jsonToVec<std::string>(*it);
                } else {
                    addrs.push_back(it->asString());
                }
                result.emplace(it.name(), std::move(addrs));
            }
        }
        return result;
    }

    std::map<std::string, UnboundEnabler::NamesToIPs> parseZones(const Json::Value& src) {
        std::map<std::string, UnboundEnabler::NamesToIPs> result;
        if (src.isObject()) {
            const auto end = src.end();
            for (auto it = src.begin(); it != end; ++it) {
                result.emplace(it.name(), convertNamesToIPs(*it));
            }
        }
        return result;
    }

    class NetworkdReconfigurer: public UnboundEnabler::Reconfigurer {
        std::shared_ptr<quasar::ipc::IConnector> networkdConnector_;

    public:
        NetworkdReconfigurer(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory)
            : networkdConnector_(ipcFactory->createIpcConnector("networkd"))
        {
            networkdConnector_->connectToService();
        }

        void reconfigureNetwork() override {
            YIO_LOG_INFO("Asking networkd to reconfig");
            networkdConnector_->sendMessage(
                quasar::ipc::buildMessage([](auto& msg) {
                    msg.mutable_network_config_reload();
                }));
        }
    };
} // namespace

UnboundEnabler::UnboundEnabler(ReconfigurerPtr reconfigurer, std::shared_ptr<YandexIO::ITelemetry> telemetry)
    : reconfigurer_(std::move(reconfigurer))
    , telemetry_(std::move(telemetry))
{
}

void UnboundEnabler::disableUnbound() {
    disableUnboundTrigger();
    reconfigurer_->reconfigureNetwork();
    telemetry_->reportEvent("UnboundDnsDisabled");
}

void UnboundEnabler::delayResolveCheck() {
    YIO_LOG_DEBUG("Delaying resolves check for unbound for " << cfg_.delayToCheck.count() << " seconds");
    callbackQueue_.addDelayed([this]() {
        for (const auto& host : cfg_.hostsToResolve) {
            if (auto addr = tryToResolve(host)) {
                YIO_LOG_INFO(host << " was successfully resolved to " << addr.value() << "! Unbound configured as local DNS in system!");
                telemetry_->reportEvent("UnboundDnsResolveCheckOk");
                return;
            }
        }
        YIO_LOG_INFO("No hosts from list of " << cfg_.hostsToResolve.size() << " was resolved. Returning to external DNS");
        previousAttemptFailed_ = true;
        telemetry_->reportEvent("UnboundDnsResolveCheckFail");
        disableUnbound();
    }, cfg_.delayToCheck);
}

bool UnboundEnabler::parseConfig(const std::string& jsonConfigValue, Config& dst) {
    auto cfgOpt = quasar::tryParseJson(jsonConfigValue);
    if (cfgOpt && cfgOpt->isMember("checkHosts")) {
        const auto& cfg = *cfgOpt;
        dst.hostsToResolve = quasar::jsonToVec<std::string>(cfg["checkHosts"]);
        dst.forwardFirst = quasar::getOptionalBool(cfg, "forwardFirst");
        dst.forwardNoCache = quasar::getOptionalBool(cfg, "forwardNoCache");
        dst.forwarders = quasar::jsonToVec<std::string>(cfg["forwarders"]);
        dst.randomizeForwarders = quasar::tryGetBool(cfg, "randomizeForwarders", false);
        dst.zones = parseZones(cfg["zones"]);
        dst.serverParams = quasar::jsonToVec<std::string>(cfg["serverParams"]);
        dst.delayToCheck = std::chrono::seconds(quasar::tryGetUInt32(cfg, "checkDelaySec", 60));
        return !dst.hostsToResolve.empty() && quasar::tryGetBool(cfg, "enable", false);
    }
    return false;
}

std::string UnboundEnabler::buildUnboundConf(const Config& cfg) {
    std::string result = "server:\n";
    for (const auto& param : cfg.serverParams) {
        result += '\t';
        result += param;
        result += '\n';
    }
    for (const auto& [zone, hosts] : cfg.zones) {
        if (!zone.empty() && !hosts.empty()) {
            std::string hostsCfg;
            for (const auto& [hostname, addrs] : hosts) {
                if (hostname.ends_with(zone) && !addrs.empty()) {
                    for (const auto& addr : addrs) {
                        if (!addr.empty()) {
                            hostsCfg += "\tlocal-data: \"";
                            hostsCfg += hostname;
                            hostsCfg += " A ";
                            hostsCfg += addr;
                            hostsCfg += "\"\n";
                        }
                    }
                }
            }
            if (!hostsCfg.empty()) {
                result += "\tlocal-zone: ";
                result += zone;
                result += " transparent\n";
                result += hostsCfg;
            }
        };
    }
    result += "forward-zone:\n\tname: \".\"\n";
    result += yesOrNoParam(cfg.forwardFirst, "\tforward-first");
    result += yesOrNoParam(cfg.forwardNoCache, "\tforward-no-cache");
    auto appendForwarders = [&result](auto forwarders) {
        for (const auto& param : forwarders) {
            result += "\tforward-addr: \"";
            result += param;
            result += "\"\n";
        }
    };
    if (cfg.randomizeForwarders) {
        auto forwarders = cfg.forwarders;
        Shuffle(std::begin(forwarders), std::end(forwarders));
        appendForwarders(forwarders);
    } else {
        appendForwarders(cfg.forwarders);
    }
    return result;
}

bool UnboundEnabler::configHeadChanged() const {
    if (cfg_.forwarders.empty() && cfg_.serverParams.empty() && cfg_.zones.empty()) {
        if (quasar::fileExists(CUSTOM_HEAD_PATH)) {
            YIO_LOG_INFO(CUSTOM_HEAD_PATH << " was removed");
            unlink(CUSTOM_HEAD_PATH);
            return true;
        }
        return false;
    }

    const auto newConfig = buildUnboundConf(cfg_);
    try {
        const auto oldConfig = quasar::getFileContent(CUSTOM_HEAD_PATH);
        if (oldConfig == newConfig) {
            return false;
        }
    } catch (...) {
    }
    storeCustomHead(newConfig);
    YIO_LOG_INFO(CUSTOM_HEAD_PATH << " was changed");
    return true;
}

void UnboundEnabler::processConfig(const std::string& jsonConfigValue) {
    Config newConfig;
    const bool shouldBeEnabled = parseConfig(jsonConfigValue, newConfig);
    if (newConfig == cfg_ && shouldBeEnabled == currentlyEnabled()) {
        YIO_LOG_DEBUG("Config isn't changed");
        return;
    }
    cfg_ = std::move(newConfig);
    YIO_LOG_INFO("Unbound dns server " << (shouldBeEnabled ? "enabled" : "disabled") << " in config");
    if (shouldBeEnabled) {
        const bool headChanged = configHeadChanged();
        if (!headChanged && previousAttemptFailed_) {
            YIO_LOG_INFO("Do not enabled cos previous attempt was failed");
            return;
        }
        if (!currentlyEnabled() || headChanged) {
            enableUnbound();
            reconfigurer_->reconfigureNetwork();
            telemetry_->reportEvent("UnboundDnsEnabled");
            delayResolveCheck();
        }
    } else {
        if (currentlyEnabled()) {
            disableUnbound();
        }
    }
}

void UnboundEnabler::onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) {
    if (configName != CONFIG_NAME) {
        return;
    }

    callbackQueue_.add([this, jsonConfigValue]() {
        processConfig(jsonConfigValue);
    });
}

UnboundEnabler::ReconfigurerPtr makeNetworkdReconfigurer(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory) {
    return std::make_shared<NetworkdReconfigurer>(ipcFactory);
}

std::shared_ptr<UnboundEnabler> makeUnboundEnabler(YandexIO::SDKInterface& sdk, std::shared_ptr<YandexIO::ITelemetry> telemetry, UnboundEnabler::ReconfigurerPtr reconfigurer) {
    auto unboundEnabler = std::make_shared<UnboundEnabler>(std::move(reconfigurer), telemetry);
    sdk.addBackendConfigObserver(unboundEnabler);
    sdk.subscribeToSystemConfig(CONFIG_NAME);
    return unboundEnabler;
}
