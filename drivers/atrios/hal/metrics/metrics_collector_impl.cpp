#include "metrics_collector_impl.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/metrica/diskstats/disk_stats_info.h>

#include <array>
#include <fstream>
#include <cstdlib>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

using namespace quasar;

namespace {
    constexpr const char* EMMC_LIFETIME_SYSFS_PATH = "/sys/class/mmc_host/emmc/emmc:0001/life_time";
    constexpr const char* EMMC_PRE_EOL_SYSFS_PATH = "/sys/class/mmc_host/emmc/emmc:0001/pre_eol_info";
    constexpr const char* LED_CONTROLLER_NAME = "fb4";
    constexpr const char* WIFI_NAME = "iwlwifi_1";

    std::string getHwMonitorName(const std::string& dir) {
        try {
            return trim(getFileContent("/sys/class/hwmon/" + dir + "/name"));
        } catch (std::exception& e) {
            return "";
        }
    }
} // namespace

MetricsCollectorImpl::MetricsCollectorImpl(std::shared_ptr<YandexIO::IDevice> device, const std::shared_ptr<ipc::IIpcFactory>& ipcFactory,
                                           std::shared_ptr<PingManager> pingManager, std::chrono::milliseconds period)
    : MetricsCollectorBase(std::move(device), ipcFactory, std::move(pingManager),
                           {{"/sys/class/thermal/thermal_zone0/temp", {"SOC", 0.001}}, {"/sys/class/thermal/thermal_zone1/temp", {"DDR", 0.001}}})
{
    executor_ = std::make_unique<quasar::PeriodicExecutor>(
        std::bind(&MetricsCollectorImpl::collectSystemMetrics, this),
        period);
    lastCpuFreqSentTimepoint_ = std::chrono::steady_clock::now();
    initHwMonitors();
}

void MetricsCollectorImpl::collectPlatformMetrics(IMetricConsumer& metrics)
{
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

    if (hwMonitors_.contains(LED_CONTROLLER_NAME)) {
        collectLedControllerHardwareMetrics(metrics, hwMonitors_[LED_CONTROLLER_NAME]);
    }
    if (hwMonitors_.contains(WIFI_NAME)) {
        collectWifiHardwareMetrics(metrics, hwMonitors_[WIFI_NAME]);
    }

    DiskStatsInfo diskStatsInfo = DiskStatsInfo::getDiskStatsInfo(disksList_, uptime());
    for (const auto& diskDev : diskStatsInfo.devices)
    {
        metrics.addDGauge(diskDev.name + "-readPerSecKB", diskDev.readsPerSecKB());
        metrics.addDGauge(diskDev.name + "-writePerSecKB", diskDev.writesPerSecKB());
        metrics.addIGauge(diskDev.name + "-readSectors", diskDev.stats.rdSectors);
        metrics.addIGauge(diskDev.name + "-writeSectors", diskDev.stats.wrSectors);
    }

    if (auto emmcInfo = getEmmcInfo()) {
        metrics.addIGauge("emmcLifetimeA", emmcInfo->lifetimeTypeA);
        metrics.addIGauge("emmcLifetimeB", emmcInfo->lifetimeTypeB);
        metrics.addIGauge("emmcPreEOL", emmcInfo->preEOLinfo);
    }
}

std::optional<MetricsCollectorImpl::EmmcStatInfo> MetricsCollectorImpl::getEmmcInfo() {
    EmmcStatInfo emmcInfo;

    std::ifstream emmcLifetime(EMMC_LIFETIME_SYSFS_PATH);
    if (emmcLifetime.good()) {
        emmcLifetime >> std::hex >> emmcInfo.lifetimeTypeA;
        emmcLifetime >> std::hex >> emmcInfo.lifetimeTypeB;
    } else {
        YIO_LOG_ERROR_EVENT("MetricsCollectorImpl.ReadEmmcLifetimeFailed", "Failed to read sysfs node: " << EMMC_LIFETIME_SYSFS_PATH);
        return std::nullopt;
    }

    std::ifstream emmcPreEOL(EMMC_PRE_EOL_SYSFS_PATH);
    if (emmcPreEOL.good()) {
        emmcPreEOL >> emmcInfo.preEOLinfo;
    } else {
        YIO_LOG_ERROR_EVENT("MetricsCollectorImpl.ReadEmmcPreEolFailed", "Failed to read sysfs node: " << EMMC_PRE_EOL_SYSFS_PATH);
        return std::nullopt;
    }

    return emmcInfo;
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

void MetricsCollectorImpl::collectLedControllerHardwareMetrics(IMetricConsumer& metrics, const std::string& hwMon) {
    const auto path = "/sys/class/hwmon/" + hwMon;
    try {
        const double temp = std::stoi(getFileContent(path + "/temp1_input")) / 1000.0;
        metrics.addDGauge("ledControllerTemperature", temp);
    } catch (std::exception& e) {
    }

    try {
        const int overflows = std::stoi(getFileContent(path + "/frame_queue_overflows"));
        metrics.addIGauge("ledControllerInternalOverflows", overflows);
    } catch (std::exception& e) {
    }

    try {
        const int framesInQueue = std::stoi(getFileContent(path + "/frames_in_frame_queue"));
        metrics.addIGauge("ledControllerInternalQueue", framesInQueue);
    } catch (std::exception& e) {
    }
}

void MetricsCollectorImpl::collectWifiHardwareMetrics(IMetricConsumer& metrics, const std::string& hwMon) {
    const auto path = "/sys/class/hwmon/" + hwMon;
    try {
        const double temp = std::stoi(getFileContent(path + "/temp1_input")) / 1000.0;
        metrics.addDGauge("wifiTemperature", temp);
    } catch (std::exception& e) {
    }
}

void MetricsCollectorImpl::initHwMonitors()
{
    try {
        const auto dirs = getDirectoryFileList("/sys/class/hwmon/");
        for (const auto& dir : dirs) {
            if (dir.starts_with(".")) {
                continue;
            }

            const auto name = getHwMonitorName(dir);
            if (!name.empty()) {
                hwMonitors_[name] = dir;
                YIO_LOG_INFO("Found hwMonitor[" << name << "]: " << dir);
            }
        }
    } catch (std::exception& e) {
        YIO_LOG_ERROR_EVENT("MetricsCollectorImpl.InitHwMonitorsFailed", "Failed to init hardware monitors: " << e.what());
    }
}
