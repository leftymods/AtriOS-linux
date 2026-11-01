#include "wifi_reseter.h"
#include "settings.h"

#include <yandex_io/protos/quasar_proto.pb.h>
#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/net/netlink_monitor.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/triggers/time_trigger.h>
#include <yandex_io/libs/base/utils.h>

#include <mutex>
#include <condition_variable>
// open&write
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

using namespace quasar;

namespace {
    using TriggerConfig = quasar::TriggerConfig;

    constexpr std::chrono::seconds NO_COUNTERS_ALERT_INTERVAL = std::chrono::minutes(3);

    std::string msPeriodAsString(auto p) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(p);
        return std::to_string(ms.count());
    }

    using TimePoint = std::chrono::steady_clock::time_point;

    class WlanWatcher: public net::NetlinkMonitor::Handler {
        using Scope = net::NetlinkMonitor::Scope;
        using IfFlags = net::NetlinkMonitor::IfFlags;

        std::unique_ptr<net::NetlinkMonitor> monitor_;
        std::function<void(const Stats&)> onStats_;
        std::mutex mutex_;
        bool wlanAbsent_{true};
        bool addressAbsent_{true};
        IfIndex wlanIdx_{0};
        IfFlags wlanFlags_;
        std::thread thread_;
        std::condition_variable cond_;

        void onAddress(IfIndex idx, std::string /*address*/, Family /*family*/, Scope scope, bool local) override {
            std::scoped_lock lock(mutex_);
            if (idx == wlanIdx_ && scope == Scope::UNIVERSAL && !local) {
                addressAbsent_ = false;
                cond_.notify_one();
            }
        }

        void onLink(IfIndex idx, std::string name, IfFlags flags) override {
            std::scoped_lock lock(mutex_);
            if (name == "wlan0") {
                wlanIdx_ = idx;
                wlanFlags_ = flags;
                wlanAbsent_ = false;
                cond_.notify_one();
            }
        }

        void onLinkRemoved(IfIndex idx) override {
            std::scoped_lock lock(mutex_);
            if (idx == wlanIdx_) {
                wlanIdx_ = 0;
                wlanAbsent_ = true;
                wlanFlags_ = IfFlags();
                addressAbsent_ = true;
                cond_.notify_one();
            }
        }

        void onAddressRemove(IfIndex /*idx*/, std::string /*addr*/, Family /*family*/, Scope /*scope*/, bool /*local*/) override{};
        void onMac(IfIndex /*idx*/, std::string /*mac*/) override{};

        void onStats(IfIndex idx, const Stats& st) override {
            if (idx == wlanIdx_) {
                onStats_(st);
            }
        }

    public:
        WlanWatcher(std::function<void(const Stats&)> onStats)
            : monitor_(net::makeNetlinkMonitor(*this))
            , onStats_(onStats)
        {
            monitor_->monitor(false);
            thread_ = std::thread(&WlanWatcher::threadLoop, this);
        }

        ~WlanWatcher() {
            monitor_->stop();
            thread_.join();
        }

        void threadLoop() {
            monitor_->monitor(true, true);
        }

        void waitWlanDown() {
            YIO_LOG_INFO("Waiting wlan down");
            std::unique_lock lock(mutex_);
            cond_.wait(lock, [this]() {
                return !wlanFlags_.up;
            });
        }

        void waitWlanDisappear() {
            YIO_LOG_INFO("Waiting wlan to disappear");
            std::unique_lock lock(mutex_);
            cond_.wait(lock, [this]() {
                return wlanAbsent_;
            });
        }

        void waitWlanAppear() {
            YIO_LOG_INFO("Waiting wlan to appear");
            std::unique_lock lock(mutex_);
            cond_.wait(lock, [this]() {
                return !wlanAbsent_;
            });
        }

        TimePoint waitWlanUp() {
            YIO_LOG_INFO("Waiting wlan up");
            std::unique_lock lock(mutex_);
            cond_.wait(lock, [this]() {
                return !wlanAbsent_ && wlanFlags_.up;
            });
            YIO_LOG_INFO("Waiting wlan addresses");
            auto wlanUpTime = std::chrono::steady_clock::now();
            cond_.wait(lock, [this]() {
                return !addressAbsent_;
            });
            return wlanUpTime;
        }
    };

    std::string methodToString(WifiReseterSettings::Method method) {
        switch (method) {
            case WifiReseterSettings::Method::HARD:
                return "hard";
            case WifiReseterSettings::Method::OLD:
                return "old";
            default:
                break;
        }
        return "unset";
    }

    class WifiReseterImpl: public WifiReseter, public YandexIO::BackendConfigObserver, public YandexIO::SDKStateObserver {
        std::shared_ptr<YandexIO::ITelemetry> telemetry_;
        std::shared_ptr<IWifiSwitcher> wifiSwitcher_;
        TimePoint prevReset_;
        TimePoint lastCountersTime_;
        WifiReseterSettings settings_;
        bool noCountersTooLongTelemetrySent_{false};
        using Method = WifiReseterSettings::Method;

        struct Statistics {
            quasar::TooLowTrigger rx;
            quasar::TooLowTrigger tx;
            quasar::TooHighTrigger txErrors;
            quasar::TooHighTrigger rxErrors;
            quasar::FalseTooLong internet;
        } stats_;

        struct Triggered {
            bool rx{false};
            bool tx{false};
            bool rxErrors{false};
            bool txErrors{false};

            void reset() {
                rx = tx = rxErrors = txErrors = false;
            }

            std::tuple<std::string, Method> getReason(const WifiReseterSettings& settings) const {
                if (rx) {
                    return {"rx", settings.minRx.method};
                }
                if (tx) {
                    return {"tx", settings.minTx.method};
                }
                if (rxErrors) {
                    return {"rxErrors", settings.maxRxErrors.method};
                }
                if (txErrors) {
                    return {"txErrors", settings.maxTxErrors.method};
                }
                return {std::string(), Method::UNSET};
            }
        } triggered_;

        OnceADayTrigger dailyTrigger_;
        quasar::NamedCallbackQueue callbackQueue_{"WifiReseter"};
        WlanWatcher wlanWatcher_;
        bool isFirstCounters_{true};

        static void writeOneTo(const char* fileName) {
            int procFd = open(fileName, O_WRONLY);
            if (procFd >= 0) {
                char one = '1';
                write(procFd, &one, 1);
                close(procFd);
            }
        }

        void doHardReset(const std::string& reason) {
            wifiSwitcher_->turnWifiOff();

            const auto startTime = std::chrono::steady_clock::now();
            wlanWatcher_.waitWlanDown(); // FIXME: possible races if interface will up before entering here

            const auto wlanOffTIme = std::chrono::steady_clock::now();
            if (wifiSwitcher_->needPciRemove()) {
                YIO_LOG_INFO("Touching proc");
                writeOneTo("/sys/bus/pci/devices/0000:00:00.0/remove");
                wlanWatcher_.waitWlanDisappear();
                updateCounters(0, 0, 0, 0); // reseting counters
                writeOneTo("/sys/bus/pci/rescan");
                wlanWatcher_.waitWlanAppear();
            }

            wifiSwitcher_->turnWifiOn();

            const auto wlanUpTime = wlanWatcher_.waitWlanUp();
            const auto addrUpTime = std::chrono::steady_clock::now();

            telemetry_->reportKeyValues("wifiReset",
                                        {
                                            {"offMs", msPeriodAsString(wlanOffTIme - startTime)},
                                            {"onMs", msPeriodAsString(wlanUpTime - wlanOffTIme)},
                                            {"addrMs", msPeriodAsString(addrUpTime - wlanUpTime)},
                                            {"total", msPeriodAsString(addrUpTime - startTime)},
                                            {"method", "hard"},
                                            {"reason", reason},
                                        });
        }

        void doOldReset(const std::string& reason) {
            const auto startTime = std::chrono::steady_clock::now();
            wifiSwitcher_->oldResetViaReconfigure();
            telemetry_->reportKeyValues("wifiReset",
                                        {
                                            {"total", msPeriodAsString(std::chrono::steady_clock::now() - startTime)},
                                            {"method", "old"},
                                            {"reason", reason},
                                        });
        }

        void startReseting(const std::string reason, Method method) {
            if (method == Method::UNSET) {
                YIO_LOG_WARN("Trying to reset with unspecified method. Ignoring.");
                return;
            }

            if (auto timeDiff = std::chrono::steady_clock::now() - prevReset_; timeDiff < settings_.minResetInterval) {
                YIO_LOG_INFO("Do not reset wifi for reason "
                             << reason << ". Previous reset was recently: "
                             << std::chrono::duration_cast<std::chrono::seconds>(timeDiff).count()
                             << " seconds ago "
                             << ". Minimum interval is " << settings_.minResetInterval.count());
                return;
            }
            YIO_LOG_INFO("Reseting wifi. Reason is " << reason << ", method is " << methodToString(method));

            if (method == Method::HARD) {
                doHardReset(reason);
            } else {
                doOldReset(reason);
            }

            YIO_LOG_INFO("Reseting completed");
            prevReset_ = std::chrono::steady_clock::now();
        }

        bool noInternetTooLong() const {
            const auto now = std::chrono::steady_clock::now();
            return (settings_.noInternet.has_value() && stats_.internet.check(now, settings_.noInternet.value().interval));
        }

        void updateCounters(unsigned rx, unsigned tx, unsigned rxErrors, unsigned txErrors) {
            YIO_LOG_DEBUG("new counters " << rx << ' ' << tx << ' ' << rxErrors << ' ' << txErrors);
            auto now = std::chrono::steady_clock::now();
            if (noCountersTooLongTelemetrySent_) {
                lastCountersTime_ = now;
                noCountersTooLongTelemetrySent_ = false;
                telemetry_->reportEvent("wifiResetCountersRestored");
            }
            triggered_.rx = stats_.rx.update(rx, now, settings_.minRx);
            triggered_.tx = stats_.tx.update(tx, now, settings_.minTx);
            triggered_.rxErrors = stats_.rxErrors.update(rxErrors, now, settings_.maxRxErrors);
            triggered_.txErrors = stats_.txErrors.update(txErrors, now, settings_.maxTxErrors);
        }

        void checkCounters() {
            auto [reason, method] = triggered_.getReason(settings_);
            if (!reason.empty() && method != Method::UNSET) {
                callbackQueue_.add([this, reason = std::move(reason), method = method] {
                    startReseting(reason, method);
                    triggered_.reset();
                });
            }
        }

        std::tuple<std::string, Method> checkReasonForReset() {
            if (noInternetTooLong()) {
                return {"nointernet", settings_.noInternet.value().method};
            }
            if (settings_.autoReset && std::chrono::steady_clock::now() - prevReset_ > settings_.autoReset.value().interval) {
                return {"autoreset", settings_.autoReset.value().method};
            }
            if (settings_.dailyReset && dailyTrigger_.check(std::chrono::system_clock::now())) {
                return {"daily", settings_.dailyReset.value().method};
            }
            return {};
        }

        bool noCountersTooLong() {
            return std::chrono::steady_clock::now() - lastCountersTime_ > NO_COUNTERS_ALERT_INTERVAL;
        }

        void oneMinuteCheck() {
            auto [reason, method] = checkReasonForReset();
            if (!reason.empty() && method != Method::UNSET) {
                callbackQueue_.add([this, reason = std::move(reason), method = method] {
                    startReseting(reason, method);
                });
            }

            if (!noCountersTooLongTelemetrySent_ && noCountersTooLong()) {
                noCountersTooLongTelemetrySent_ = true;
                telemetry_->reportEvent("wifiResetNoCountersTooLong");
            }
        }

        void delayOneMinuteCheck() {
            callbackQueue_.addDelayed([this]() {
                try {
                    oneMinuteCheck();
                } catch (...) {
                }
                delayOneMinuteCheck();
            }, std::chrono::minutes(1));
        }

    public:
        WifiReseterImpl(std::shared_ptr<YandexIO::ITelemetry> telemetry,
                        std::shared_ptr<IWifiSwitcher> wifiSwitcher)
            : telemetry_(std::move(telemetry))
            , wifiSwitcher_(std::move(wifiSwitcher))
            , prevReset_(std::chrono::steady_clock::now())
            , lastCountersTime_(prevReset_)
            , wlanWatcher_([this](const WlanWatcher::Stats& st) {
                callbackQueue_.add([this, st] {
                    updateCounters(st.rx_packets, st.tx_packets, st.rx_errors, st.tx_errors);
                    if (isFirstCounters_) {
                        triggered_.reset();
                        isFirstCounters_ = false;
                    } else {
                        checkCounters();
                    }
                });
            })
        {
            delayOneMinuteCheck();
        }

        void onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) override {
            if (configName != "wifi_reseter2") {
                return;
            }
            auto cfg = quasar::parseJson(jsonConfigValue);
            callbackQueue_.add([this, cfg = std::move(cfg)]() {
                YIO_LOG_INFO("New settings came");
                settings_ = wifiReseterSettingsFromJson(cfg);
                if (settings_.dailyReset) {
                    dailyTrigger_.changeTime(std::chrono::duration_cast<std::chrono::minutes>(settings_.dailyReset.value().interval));
                }
            });
        };

        void onSDKState(const YandexIO::SDKState& state) override {
            callbackQueue_.add([this, hasInternet = state.wifiState.isInternetReachable, now = std::chrono::steady_clock::now()] {
                YIO_LOG_DEBUG("Internet is " << (hasInternet ? "available" : "absent"));
                stats_.internet.update(hasInternet, now);
            });
        }

        void resetWifiByButton() override {
            callbackQueue_.add([this] {
                if (settings_.sixClicksEnabled) {
                    startReseting("button", Method::HARD);
                } else {
                    YIO_LOG_INFO("Reseting wifi by button is disabled");
                }
            });
        }
    };

    class WifiSwitcherBase: public IWifiSwitcher {
    protected:
        std::shared_ptr<quasar::ipc::IConnector> wifidConnector_;
        std::shared_ptr<quasar::ipc::IConnector> networkdConnector_;

    public:
        WifiSwitcherBase(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory)
            : wifidConnector_(ipcFactory->createIpcConnector("wifid"))
            , networkdConnector_(ipcFactory->createIpcConnector("networkd"))
        {
            wifidConnector_->setMessageHandler([this](const auto& message) {
                onMessage(message);
            });
            wifidConnector_->connectToService();
            networkdConnector_->connectToService();
        }

        void oldResetViaReconfigure() override {
            networkdConnector_->sendMessage(quasar::ipc::buildMessage([](auto& msg) {
                msg.mutable_network_config_reload();
            }));
        }

        virtual void onMessage(const ipc::SharedMessage& /*message*/) {
            // do nothing
        }
    };

    class MinisWifiSwitcher: public WifiSwitcherBase {
        std::mutex mutex_;
        std::string answerId_;
        std::condition_variable cond_;

        void setAnswerId(const std::string& id) {
            std::scoped_lock<std::mutex> lock(mutex_);
            answerId_ = id;
        }

        void onMessage(const ipc::SharedMessage& message) override {
            if (message->has_request_id()) {
                std::scoped_lock<std::mutex> lock(mutex_);
                if (!answerId_.empty() && answerId_ == message->request_id()) {
                    answerId_.clear();
                    cond_.notify_all();
                }
            }
        }

        void waitAnswer() {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait(lock, [this]() {
                return answerId_.empty();
            });
        }

    public:
        MinisWifiSwitcher(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory)
            : WifiSwitcherBase(ipcFactory)
        {
        }

        void turnWifiOff() override {
            YIO_LOG_INFO("Sending wifi_networks_disable");
            auto request = quasar::ipc::buildMessage([](auto& msg) {
                msg.set_request_id(quasar::makeUUID());
                msg.mutable_wifi_networks_disable();
            });
            setAnswerId(request->request_id());
            wifidConnector_->sendMessage(request);
            YIO_LOG_DEBUG("Waiting for answer for " << request->request_id());
            waitAnswer();
            system("ip link set wlan0 down"); // wpa_supplicant leaves interface in UP state but without address
        }

        void turnWifiOn() override {
            YIO_LOG_INFO("Sending wifi_networks_enable");
            auto request = quasar::ipc::buildMessage([](auto& msg) {
                msg.set_request_id(quasar::makeUUID());
                msg.mutable_wifi_networks_enable();
            });
            setAnswerId(request->request_id());
            wifidConnector_->sendMessage(request);
            waitAnswer();
        }

        bool needPciRemove() override {
            return false;
        }
    };

    class StationWifiSwitcher: public WifiSwitcherBase {
    public:
        StationWifiSwitcher(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory)
            : WifiSwitcherBase(ipcFactory)
        {
        }

        void turnWifiOff() override {
            YIO_LOG_INFO("Sending wifi_turn_off");
            wifidConnector_->sendMessage(quasar::ipc::buildMessage([](auto& msg) {
                msg.mutable_wifi_turn_off();
            }));
        }

        void turnWifiOn() override {
            YIO_LOG_INFO("Sending wifi_turn_on");

            system("svc wifi enable");
            wifidConnector_->sendMessage(quasar::ipc::buildMessage([](auto& msg) {
                msg.mutable_wifi_turn_on();
            }));
        }

        bool needPciRemove() override {
            return true;
        }
    };
} // namespace

std::shared_ptr<IWifiSwitcher> quasar::makeStationWifiSwitcher(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory) {
    return std::make_shared<StationWifiSwitcher>(ipcFactory);
}

std::shared_ptr<IWifiSwitcher> quasar::makeMinisWifiSwitcher(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory) {
    return std::make_shared<MinisWifiSwitcher>(ipcFactory);
}

std::shared_ptr<WifiReseter> quasar::makeWifiReseter(std::shared_ptr<YandexIO::SDKInterface> sdk,
                                                     std::shared_ptr<YandexIO::ITelemetry> telemetry,
                                                     std::shared_ptr<IWifiSwitcher> wifiSwitcher) {
    auto result = std::make_shared<WifiReseterImpl>(std::move(telemetry), std::move(wifiSwitcher));
    sdk->addSDKStateObserver(result);
    sdk->addBackendConfigObserver(result);
    sdk->subscribeToSystemConfig("wifi_reseter2");
    return result;
}
