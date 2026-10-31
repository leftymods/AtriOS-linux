#pragma once

#include <yandex_io/libs/telemetry/telemetry.h>

#include <chrono>
#include <memory>
#include <map>
#include <string>
#include <mutex>
#include <optional>

namespace YandexIO {

    class AudioDeviceStats {
    public:
        enum class Tag {
            DO_CAPTURE,
            READ_HW_MIC,
            READ_HW_SPK,
            READ_HW_MIC_SPK,
            READ_SW_LB,
            READ_HDMI_LB,
            READ_HW_MIC_STUB,
            ASYNC_LAG,
            PROCESSING
        };

        using Clock = std::chrono::steady_clock;
        using TagDurations = std::map<Tag, std::chrono::nanoseconds>;

        class ChunkSession {
        public:
            explicit ChunkSession(AudioDeviceStats& stats);
            ~ChunkSession();

            void start(Tag tag);
            void stop(Tag tag);

        private:
            AudioDeviceStats& stats_;

            std::map<Tag, Clock::time_point> tagStarts_;
            TagDurations tagDurations_;
        };

        AudioDeviceStats(std::shared_ptr<YandexIO::ITelemetry> telemetry, std::chrono::milliseconds minInterval = std::chrono::minutes(10));

        std::shared_ptr<ChunkSession> createChunkSession(int processQueueSize);
        void addTagDurations(const TagDurations& tagDurations);

    private:
        void sendStats();

    private:
        struct TagStats {
            std::chrono::nanoseconds min;
            std::chrono::nanoseconds max;
            std::chrono::nanoseconds avg;
            uint32_t count;

            explicit TagStats(std::chrono::nanoseconds diff);
        };
        struct Stats {
            std::map<Tag, TagStats> tagStats;
            int minProcessQueueSize;
            int maxProcessQueueSize;
            double avgProcessQueueSize;
            uint32_t count{0};
        };
        Stats stats_;
        std::mutex mutex_;

        const std::shared_ptr<YandexIO::ITelemetry> telemetry_;
        const std::chrono::milliseconds minInterval_;
        std::optional<Clock::time_point> statsCollectionStartTime_;
    };

} // namespace YandexIO
