#pragma once

#include <yandex_io/libs/audio_player/base/audio_clock.h>

#include <json/json.h>

#include <atomic>
#include <optional>
#include <string>

namespace quasar {

    class GstreamerAudioClock: public AudioClock {
    public:
        GstreamerAudioClock(std::string deviceId, int port, std::string clockId);
        GstreamerAudioClock(std::string deviceId, std::string host, int port, std::string clockId);
        GstreamerAudioClock(const GstreamerAudioClock&) = delete;
        ~GstreamerAudioClock();
        GstreamerAudioClock& operator=(const GstreamerAudioClock&) = delete;

        void setMaxRtt(std::chrono::microseconds maxRtt) noexcept;
        void setMinUpdateInterval(std::chrono::microseconds minUpdateInterval) noexcept;
        void setMaxRttStdDev(std::chrono::microseconds maxRttStdDev) noexcept;
        void setMaxSyncTimeout(std::chrono::microseconds maxSyncTimeout) noexcept;
        Json::Value jsonStatistics() const noexcept;
        void* gstClock() const noexcept;

        struct Statistics {
            SyncLevel syncLevel = {SyncLevel::NONE};
            uint64_t limitRttMax = 0;
            uint64_t statRttAvg = 0;
            uint64_t statRttAverage = 0;
            uint64_t statRttMedian = 0;
            uint64_t statRttStdDev = 0;
            uint64_t syncTimeout = 0;
            uint64_t rateNum = 0;
            uint64_t rateDenom = 0;
        };
        Statistics statistics() const noexcept;

    public: // AudioClock methods
        std::string_view deviceId() const noexcept override;
        std::string_view host() const noexcept override;
        int port() const noexcept override;
        std::string_view clockId() const noexcept override;
        std::chrono::nanoseconds now() const noexcept override;
        SyncLevel waitForSync(std::chrono::milliseconds timeout) const noexcept override;
        SyncLevel syncLevel() const noexcept override;

    private:
        SyncLevel syncLevel(int64_t statRttStdDev, int64_t syncTimeout) const noexcept;

    private:
        const std::string deviceId_;
        const std::string host_;
        const int port_{0};
        const std::string clockId_;
        const bool local_{false};
        void* netGstClock_{nullptr};
        std::atomic<int64_t> maxRttStdDevNs_{1000000ll};
        std::atomic<int64_t> maxSyncTimeoutNs_{1000000000ll};
    };

} // namespace quasar
