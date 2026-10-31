#pragma once

#include <json/json.h>

#include <yandex_io/metrica/monitor/metrics_collector/metrics_collector.h>
#include <yandex_io/metrica/monitor/metrics_collector/ping_manager/ping_manager.h>

#include <chrono>
#include <string>
#include <vector>

namespace quasar {
    class MetricsCollectorImpl final: public quasar::MetricsCollectorBase {
    public:
        explicit MetricsCollectorImpl(std::shared_ptr<YandexIO::IDevice> device, std::shared_ptr<ipc::IIpcFactory> ipcFactory,
                                      std::shared_ptr<PingManager> pingManager, std::chrono::milliseconds period);

        ~MetricsCollectorImpl() = default;

    protected:
        void collectPlatformMetrics(IMetricConsumer& metrics) override;

        static Json::Value collectCpuFreqs(const std::vector<std::string>& cpuFreqs);
        static Json::Value collectCpuFreqs();
        static std::vector<std::string> prepareCpuFreqNodes(const std::string& suffix);

        std::string lastCpuFreqsStr_;
        std::chrono::steady_clock::time_point lastCpuFreqSentTimepoint_;
    };
} // namespace quasar
