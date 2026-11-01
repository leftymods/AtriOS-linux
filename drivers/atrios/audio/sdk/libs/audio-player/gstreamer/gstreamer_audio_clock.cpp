#include "gstreamer_audio_clock.h"

#include <yandex_io/libs/base/utils.h>
#include <gst/net/gstnetclientclock.h>

#include <atomic>
#include <stdexcept>

namespace quasar {
    namespace {

        std::atomic<int> clockCounter{0};

    } // namespace

    GstreamerAudioClock::GstreamerAudioClock(std::string deviceId, int port, std::string clockId)
        : GstreamerAudioClock(std::move(deviceId), "127.0.0.1", port, std::move(clockId))
    {
    }

    GstreamerAudioClock::GstreamerAudioClock(std::string deviceId, std::string host, int port, std::string clockId)
        : deviceId_(std::move(deviceId))
        , host_(std::move(host))
        , port_(port)
        , clockId_(std::move(clockId))
        , local_(host_ == "127.0.0.1")
    {
        auto gClockId = clockId_ + "@" + std::to_string(++clockCounter);
        netGstClock_ = gst_net_client_clock_ex_new(nullptr, host_.c_str(), port, 0, gClockId.c_str());
        if (!netGstClock_) {
            throw std::runtime_error("Fail to create clock object");
        }
        gint tos = 0x28; // IPTOS_CLASS_CS5
        g_object_set(G_OBJECT(netGstClock_), "qos-dscp", tos, NULL);
    }

    GstreamerAudioClock::~GstreamerAudioClock()
    {
        gst_object_unref(GST_OBJECT(netGstClock_));
    }

    void GstreamerAudioClock::setMaxRtt(std::chrono::microseconds maxRtt) noexcept {
        int64_t maxRttNs = std::max<int64_t>(0, std::chrono::nanoseconds(maxRtt).count());
        g_object_set(G_OBJECT(netGstClock_), "round-trip-limit", maxRttNs, NULL);
    }

    void GstreamerAudioClock::setMinUpdateInterval(std::chrono::microseconds minUpdateInterval) noexcept {
        int64_t minUpdateIntervalNs = std::max<int64_t>(0, std::chrono::nanoseconds(minUpdateInterval).count());
        g_object_set(G_OBJECT(netGstClock_), "minimum-update-interval", minUpdateIntervalNs, NULL);
    }

    void GstreamerAudioClock::setMaxRttStdDev(std::chrono::microseconds maxRttStdDev) noexcept {
        maxRttStdDevNs_ = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(maxRttStdDev).count());
    }

    void GstreamerAudioClock::setMaxSyncTimeout(std::chrono::microseconds maxSyncTimeout) noexcept {
        maxSyncTimeoutNs_ = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(maxSyncTimeout).count());
    }

    Json::Value GstreamerAudioClock::jsonStatistics() const noexcept {
        auto gstclk = static_cast<GstClock*>(netGstClock_);
        gchar* clockId = nullptr;
        gchar* address = nullptr;
        gint port = 0;
        guint64 minimumUpdateInterval = 0;
        guint64 baseTime = 0;
        gint qosDscp = 0;

        g_object_get(gstclk,
                     "clock-id", &clockId,
                     "address", &address,
                     "port", &port,
                     "minimum-update-interval", &minimumUpdateInterval,
                     "base-time", &baseTime,
                     "qos-dscp", &qosDscp,
                     NULL);

        Statistics stat = statistics();

        Json::Value result;
        result["clock-id"] = clockId;
        result["address"] = address;
        result["port"] = port;
        result["round-trip-limit"] = stat.limitRttMax;
        result["minimum-update-interval"] = minimumUpdateInterval;
        result["base-time"] = baseTime;
        result["qos-dscp"] = qosDscp;
        result["stat-rtt-avg"] = stat.statRttAvg;
        result["stat-rtt-average"] = stat.statRttAverage;
        result["stat-rtt-median"] = stat.statRttMedian;
        result["stat-rtt-std-dev"] = stat.statRttStdDev;
        result["stat-rate-num"] = stat.rateNum;
        result["stat-rate-denom"] = stat.rateDenom;
        if (stat.rateDenom) {
            result["stat-rate"] = static_cast<double>(stat.rateNum) / static_cast<double>(stat.rateDenom);
        }
        result["sync-timeout"] = stat.syncTimeout;
        result["sync"] = syncLevelToText(stat.syncLevel);

        g_free(clockId);
        g_free(address);

        return result;
    }

    void* GstreamerAudioClock::gstClock() const noexcept {
        return netGstClock_;
    }

    GstreamerAudioClock::Statistics GstreamerAudioClock::statistics() const noexcept {
        guint64 roundTripLimit = 0;
        guint64 statRttAvg = 0;
        guint64 statRttAverage = 0;
        guint64 statRttMedian = 0;
        guint64 statRttStdDev = 0;
        guint64 rateNum = 0;
        guint64 rateDenom = 0;
        guint64 syncTimeout = 0;

        g_object_get(GST_CLOCK(netGstClock_),
                     "round-trip-limit", &roundTripLimit,
                     "stat-rtt-avg", &statRttAvg,
                     "stat-rtt-average", &statRttAverage,
                     "stat-rtt-median", &statRttMedian,
                     "stat-rtt-std-dev", &statRttStdDev,
                     "stat-rate-num", &rateNum,
                     "stat-rate-denom", &rateDenom,
                     "sync-timeout", &syncTimeout,
                     NULL);

        Statistics result;
        result.syncLevel = syncLevel(statRttStdDev, syncTimeout);
        result.limitRttMax = roundTripLimit;
        result.statRttAvg = statRttAvg;
        result.statRttAverage = statRttAverage;
        result.statRttMedian = statRttMedian;
        result.statRttStdDev = statRttStdDev;
        result.rateNum = rateNum;
        result.rateDenom = rateDenom;
        result.syncTimeout = syncTimeout;

        return result;
    }

    std::string_view GstreamerAudioClock::deviceId() const noexcept {
        return deviceId_;
    }

    std::string_view GstreamerAudioClock::host() const noexcept {
        return host_;
    }

    int GstreamerAudioClock::port() const noexcept {
        return port_;
    }

    std::string_view GstreamerAudioClock::clockId() const noexcept {
        return clockId_;
    }

    std::chrono::nanoseconds GstreamerAudioClock::now() const noexcept {
        if (!gst_clock_is_synced(GST_CLOCK(netGstClock_))) {
            return std::chrono::nanoseconds{0};
        }
        return std::chrono::nanoseconds{gst_clock_get_time(GST_CLOCK(netGstClock_))};
    }

    GstreamerAudioClock::SyncLevel GstreamerAudioClock::waitForSync(std::chrono::milliseconds timeout) const noexcept {
        gst_clock_wait_for_sync(GST_CLOCK(netGstClock_), std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count());
        return syncLevel();
    }

    GstreamerAudioClock::SyncLevel GstreamerAudioClock::syncLevel() const noexcept {
        guint64 statRttStdDev = 0;
        guint64 syncTimeout = 0;

        g_object_get(GST_CLOCK(netGstClock_),
                     "stat-rtt-std-dev", &statRttStdDev,
                     "sync-timeout", &syncTimeout,
                     NULL);
        return syncLevel(statRttStdDev, syncTimeout);
    }

    GstreamerAudioClock::SyncLevel GstreamerAudioClock::syncLevel(int64_t statRttStdDev, int64_t syncTimeout) const noexcept {
        if (!gst_clock_is_synced(GST_CLOCK(netGstClock_))) {
            return SyncLevel::NONE;
        }

        if (local_) {
            return SyncLevel::STRONG;
        }

        if (statRttStdDev > maxRttStdDevNs_ || syncTimeout > maxSyncTimeoutNs_) {
            return SyncLevel::WEAK;
        }

        return SyncLevel::STRONG;
    }

} // namespace quasar
