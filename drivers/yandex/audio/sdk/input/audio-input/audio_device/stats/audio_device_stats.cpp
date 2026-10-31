#include "audio_device_stats.h"

#include <yandex_io/libs/json_utils/json_utils.h>

using namespace YandexIO;

namespace {

    std::string tagToString(AudioDeviceStats::Tag tag) {
        switch (tag) {
            case AudioDeviceStats::Tag::DO_CAPTURE:
                return "do_capture";
            case AudioDeviceStats::Tag::READ_HW_MIC:
                return "read_mics";
            case AudioDeviceStats::Tag::READ_HW_SPK:
                return "read_spk";
            case AudioDeviceStats::Tag::READ_HW_MIC_SPK:
                return "read_mics_spk";
            case AudioDeviceStats::Tag::READ_SW_LB:
                return "read_sw_lb";
            case AudioDeviceStats::Tag::READ_HDMI_LB:
                return "read_hdmi_lb";
            case AudioDeviceStats::Tag::READ_HW_MIC_STUB:
                return "read_mic_stub";
            case AudioDeviceStats::Tag::ASYNC_LAG:
                return "async_lag";
            case AudioDeviceStats::Tag::PROCESSING:
                return "vqe";
        }
    }

} // namespace

AudioDeviceStats::ChunkSession::ChunkSession(AudioDeviceStats& stats)
    : stats_(stats)
{
}

AudioDeviceStats::ChunkSession::~ChunkSession() {
    if (!tagStarts_.empty()) {
        auto now = Clock::now();
        for (const auto& [tag, startTime] : tagStarts_) {
            tagDurations_.emplace(tag, std::chrono::duration_cast<std::chrono::nanoseconds>(now - startTime));
        }
    }

    if (tagDurations_.empty()) {
        return;
    }

    stats_.addTagDurations(tagDurations_);
}

void AudioDeviceStats::ChunkSession::start(Tag tag) {
    tagStarts_[tag] = Clock::now();
}

void AudioDeviceStats::ChunkSession::stop(Tag tag) {
    if (auto it = tagStarts_.find(tag); it != tagStarts_.end()) {
        tagDurations_.emplace(tag, std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - it->second));
        tagStarts_.erase(it);
    }
}

AudioDeviceStats::AudioDeviceStats(std::shared_ptr<YandexIO::ITelemetry> telemetry, std::chrono::milliseconds minInterval)
    : telemetry_(std::move(telemetry))
    , minInterval_(minInterval)
{
}

std::shared_ptr<AudioDeviceStats::ChunkSession> AudioDeviceStats::createChunkSession(int processQueueSize) {
    std::scoped_lock guard(mutex_);

    ++stats_.count;

    if (!statsCollectionStartTime_.has_value()) {
        statsCollectionStartTime_ = Clock::now();

        stats_.avgProcessQueueSize = stats_.minProcessQueueSize = stats_.maxProcessQueueSize = processQueueSize;
    } else {
        if (processQueueSize < stats_.minProcessQueueSize) {
            stats_.minProcessQueueSize = processQueueSize;
        }
        if (processQueueSize > stats_.maxProcessQueueSize) {
            stats_.maxProcessQueueSize = processQueueSize;
        }
        stats_.avgProcessQueueSize += static_cast<double>(processQueueSize - stats_.avgProcessQueueSize) / stats_.count;
    }

    return std::make_shared<ChunkSession>(*this);
}

void AudioDeviceStats::addTagDurations(const TagDurations& tagDurations) {
    std::scoped_lock guard(mutex_);

    for (const auto& [tag, duration] : tagDurations) {
        auto it = stats_.tagStats.find(tag);
        if (it == stats_.tagStats.end()) {
            stats_.tagStats.emplace(tag, TagStats{duration});
        } else {
            auto& tagStats = it->second;
            ++tagStats.count;

            if (duration < tagStats.min) {
                tagStats.min = duration;
            }
            if (duration > tagStats.max) {
                tagStats.max = duration;
            }
            tagStats.avg += (duration - tagStats.avg) / tagStats.count;
        }
    }

    auto now = Clock::now();
    if (now - statsCollectionStartTime_.value_or(now) >= minInterval_) {
        sendStats();
        stats_.tagStats.clear();
        stats_.count = 0;
        statsCollectionStartTime_.reset();
    }
}

void AudioDeviceStats::sendStats() {
    Json::Value event;
    event["minProcessQueueSize"] = stats_.minProcessQueueSize;
    event["maxProcessQueueSize"] = stats_.maxProcessQueueSize;
    event["avgProcessQueueSize"] = stats_.avgProcessQueueSize;

    for (const auto& [tag, stats] : stats_.tagStats) {
        auto& tagJson = event[tagToString(tag)];
        tagJson["minNs"] = static_cast<uint64_t>(stats.min.count());
        tagJson["maxNs"] = static_cast<uint64_t>(stats.max.count());
        tagJson["avgNs"] = static_cast<uint64_t>(stats.avg.count());
        tagJson["count"] = stats.count;
    }
    telemetry_->reportEvent("audioDeviceStats", quasar::jsonToString(event));
}

AudioDeviceStats::TagStats::TagStats(std::chrono::nanoseconds diff)
    : min(diff)
    , max(diff)
    , avg(diff)
    , count(1)
{
}
