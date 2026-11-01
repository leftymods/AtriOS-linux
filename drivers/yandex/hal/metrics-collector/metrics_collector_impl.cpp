#include "metrics_collector_impl.h"

#include <yandex_io/external_libs/datacratic/soa/types/date.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/metrica/ubi/ubi_info.h>

using namespace quasar;

MetricsCollectorImpl::MetricsCollectorImpl(std::shared_ptr<YandexIO::IDevice> device, std::shared_ptr<ipc::IIpcFactory> ipcFactory,
                                           std::shared_ptr<PingManager> pingManager, std::chrono::milliseconds period)
    : MetricsCollectorBase(std::move(device), ipcFactory, std::move(pingManager), {{"/sys/class/thermal/thermal_zone0/temp", {"SOC", 0.001}}})
{
    executor_ = std::make_unique<quasar::PeriodicExecutor>([this]() {
        collectSystemMetrics();
    }, period);
    lastCpuFreqSentTimepoint_ = std::chrono::steady_clock::now();
}

void MetricsCollectorImpl::collectPlatformMetrics(IMetricConsumer& metrics)
{
    for (const auto& stat : UbiInfo::getNumericStats(disksList_, uptime())) {
        metrics.addIGauge(stat.first, stat.second);
    }

    /* Collect cpu info */
    Json::Value cpuFreqs = Json::nullValue;
    try {
        cpuFreqs = collectCpuFreqs();
    } catch (const std::exception& e) {
        YIO_LOG_WARN("Can't get cpu freq values: " << e.what());
    }
    /* Check that collected values */
    if (!cpuFreqs.isNull()) {
        std::string cpuFreqsStr = jsonToString(cpuFreqs);
        const auto now = std::chrono::steady_clock::now();
        const auto timediff = std::chrono::duration_cast<std::chrono::minutes>(now - lastCpuFreqSentTimepoint_);
        const bool hourPassed = (timediff >= std::chrono::hours(1));
        const bool cpuFreqsChanged = lastCpuFreqsStr_ != cpuFreqsStr;
        /* If lastCpuFreqsStr is empty -> we send value for the first time
         * If cpuFreqsChanged -> send new values to metrica
         * If hour passed -> send values as a ping
         */
        if (lastCpuFreqsStr_.empty() || cpuFreqsChanged || hourPassed) {
            /* Save new values */
            lastCpuFreqsStr_ = std::move(cpuFreqsStr);
            /* Send cpu info metrics */
            device_->telemetry()->reportEvent("cpu_info", lastCpuFreqsStr_);
            lastCpuFreqSentTimepoint_ = now;
        }
    }
}

Json::Value MetricsCollectorImpl::collectCpuFreqs()
{
    Json::Value value;
    const std::vector<std::string> curFreqs = prepareCpuFreqNodes("cpuinfo_cur_freq");
    const auto currFreqArray = collectCpuFreqs(curFreqs);
    value["current"] = currFreqArray;

    const std::vector<std::string> maxFreqs = prepareCpuFreqNodes("cpuinfo_max_freq");
    const auto maxFreqArray = collectCpuFreqs(maxFreqs);
    value["max"] = maxFreqArray;

    const std::vector<std::string> minFreqs = prepareCpuFreqNodes("cpuinfo_min_freq");
    const auto minFreqArray = collectCpuFreqs(minFreqs);
    value["min"] = minFreqArray;
    return value;
}

Json::Value MetricsCollectorImpl::collectCpuFreqs(const std::vector<std::string>& cpuFreqs)
{
    Json::Value coresArray = Json::arrayValue;
    for (size_t i = 0; i < cpuFreqs.size(); ++i) {
        Json::Value coreValue;
        coreValue["core_num"] = i;
        const std::string& freqNode = cpuFreqs[i];
        const double freq = std::stod(getFileContent(freqNode), nullptr);
        coreValue["value"] = freq;
        coresArray.append(coreValue);
    }
    return coresArray;
}

std::vector<std::string> MetricsCollectorImpl::prepareCpuFreqNodes(const std::string& suffix)
{
    std::vector<std::string> freqs = {"/sys/devices/system/cpu/cpu0/cpufreq/" + suffix,
                                      "/sys/devices/system/cpu/cpu1/cpufreq/" + suffix,
                                      "/sys/devices/system/cpu/cpu2/cpufreq/" + suffix,
                                      "/sys/devices/system/cpu/cpu3/cpufreq/" + suffix};
    return freqs;
}
