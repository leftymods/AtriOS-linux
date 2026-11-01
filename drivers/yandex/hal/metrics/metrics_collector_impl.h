#pragma once

#include <yandex_io/metrica/monitor/metrics_collector/metrics_collector.h>
#include <yandex_io/metrica/monitor/metrics_collector/ping_manager/ping_manager.h>

#include <map>
#include <optional>

namespace quasar {
    class MetricsCollectorImpl final: public MetricsCollectorBase {
    public:
        explicit MetricsCollectorImpl(std::shared_ptr<YandexIO::IDevice> device, const std::shared_ptr<ipc::IIpcFactory>& ipcFactory,
                                      std::shared_ptr<PingManager> pingManager, std::chrono::milliseconds period);

        ~MetricsCollectorImpl() = default;

    protected:
        void collectPlatformMetrics(IMetricConsumer& metrics) override;

    private:
        struct EmmcStatInfo {
            int64_t lifetimeTypeA = 0;
            int64_t lifetimeTypeB = 0;
            int64_t preEOLinfo = 0;
        };

        static std::optional<EmmcStatInfo> getEmmcInfo();

        static Json::Value collectCpuFreqs(const std::vector<std::string>& cpuFreqs);
        static Json::Value collectCpuFreqs();
        static std::vector<std::string> prepareCpuFreqNodes(const std::string& suffix);
        static void collectLedControllerHardwareMetrics(IMetricConsumer& metrics, const std::string& hwMon);
        static void collectWifiHardwareMetrics(IMetricConsumer& metrics, const std::string& hwMon);
        void initHwMonitors();

        std::string lastCpuFreqsStr_;
        std::chrono::steady_clock::time_point lastCpuFreqSentTimepoint_;
        std::map<std::string, std::string> hwMonitors_;
    };
} // namespace quasar
