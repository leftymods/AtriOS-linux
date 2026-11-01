#include "audio_clock_manager.h"

#include <yandex_io/libs/audio_player/gstreamer/gstreamer_audio_clock.h>
#include <yandex_io/libs/audio_player/gstreamer/gstreamer_time_provider.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/cryptography/digest.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <cmath>
#include <random>
#include <thread>
#include <type_traits>

YIO_DEFINE_LOG_MODULE("audio_clock_manager");

using namespace quasar;

namespace {
    constexpr std::chrono::milliseconds SYNC_TIMEOUT{5000};

    template <typename T>
    T nanosecondsToMicroseconds(T nanoseconds)
    {
        T mod = nanoseconds % 1000;
        if constexpr (std::is_signed<T>()) {
            return nanoseconds / 1000 + (mod >= 500 ? 1 : (mod <= -500 ? -1 : 0));
        } else {
            return nanoseconds / 1000 + (mod >= 500 ? 1 : 0);
        }
    }

    template <typename T>
    int64_t toMicroseconds(T duration)
    {
        return nanosecondsToMicroseconds(std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
    }

} // namespace

std::string AudioClockManager::generateClockId() noexcept {
    return rndMark(6);
}

AudioClockManager::AudioClockManager(std::string deviceId, int netAudioClockPort, std::string clockId)
    : clockId_(std::move(clockId))
    , netAudioClockPort_(netAudioClockPort)
    , netTimeProvider_(std::make_shared<GstreamerTimeProvider>(netAudioClockPort))
    , localAudioClock_(std::make_shared<GstreamerAudioClock>(std::move(deviceId), netAudioClockPort, clockId_))
{
    YIO_LOG_DEBUG("Starting up AudioClockManager at port " << netAudioClockPort_ << " with clock id " << clockId_ << ", ptr=" << localAudioClock_.get());
    if (localAudioClock_->waitForSync(SYNC_TIMEOUT) == AudioClock::SyncLevel::NONE) {
        YIO_LOG_ERROR_EVENT("AudioClockManager.LocalClockSynchronization", "Fail to synchronize local clock");
        throw std::runtime_error("Fail to synchronize local clock");
    }
    YIO_LOG_DEBUG("AudioClockManager started");
}

std::shared_ptr<const AudioClock> AudioClockManager::localAudioClock() const noexcept {
    return localAudioClock_;
}

std::shared_ptr<const AudioClock> AudioClockManager::netAudioClock(std::string deviceId, std::string host, int port, std::string clockId) const {
    YIO_LOG_DEBUG("Request net audio clock for device " << deviceId << " \"" << host << ":" << port << "\" clockId=" << clockId);

    if (deviceId.empty() || host.empty() || port <= 0 || clockId.empty()) {
        YIO_LOG_WARN("Fail to get net audio clock. Invalid params: deviceId=" << deviceId << ", host=" << host << ", port=" << port << ", clockId=" << clockId);
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    std::string peer = makeString(host, ":", port);
    auto it = netAudioClocks_.find(peer);
    if (it != netAudioClocks_.end()) {
        if (it->second->clockId() == clockId) {
            return it->second;
        } else {
            YIO_LOG_INFO("Clock id was changed, old clockId=" << it->second->clockId() << " ---> new clockId=" << clockId);
        }
    }
    it = std::find_if(netAudioClocks_.begin(), netAudioClocks_.end(), [&](const auto& p) { return p.second->deviceId() == deviceId; });
    if (it != netAudioClocks_.end()) {
        YIO_LOG_INFO("Device " << deviceId << " change clock peer " << it->second->host() << ":" << it->second->port() << " ---> " << peer);
        netAudioClocks_.erase(it);
    }
    auto clock = std::make_shared<GstreamerAudioClock>(std::move(deviceId), std::move(host), port, std::move(clockId));
    clock->setMaxRtt(rttMax_);
    clock->setMaxRttStdDev(rttStdDevMax_);
    clock->setMaxSyncTimeout(syncTimeout_);
    clock->setMinUpdateInterval(minUpdateInterval_);
    netAudioClocks_[peer] = clock;

    YIO_LOG_INFO("New net clock created: " << jsonToString(clock->jsonStatistics()));
    return clock;
}

std::shared_ptr<const AudioClock> AudioClockManager::netAudioClock(std::string_view clockId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (localAudioClock_->clockId() == clockId) {
        return localAudioClock_;
    }
    for (const auto& p : netAudioClocks_) {
        if (p.second->clockId() == clockId) {
            return p.second;
        }
    }
    return nullptr;
}

std::vector<std::shared_ptr<const AudioClock>> AudioClockManager::allNetAudioClocks() const noexcept {
    std::vector<std::shared_ptr<const AudioClock>> result;
    result.reserve(netAudioClocks_.size());
    for (const auto& p : netAudioClocks_) {
        result.push_back(p.second);
    }
    return result;
}

int AudioClockManager::netAudioClockPort() const noexcept {
    return netAudioClockPort_;
}

std::string_view AudioClockManager::localClockId() const noexcept {
    return clockId_;
}

std::optional<AudioClockManager::ClockSyncParams> AudioClockManager::clockSyncParams(std::string_view clockId) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(netAudioClocks_.begin(), netAudioClocks_.end(), [&](const auto& p) { return p.second->clockId() == clockId; });
    if (it == netAudioClocks_.end()) {
        return std::nullopt;
    }

    int64_t localClock = localAudioClock_->now().count();
    int64_t netClock = it->second->now().count();
    auto stat = it->second->statistics();
    Json::Value jsonStat;
    jsonStat["sync"] = GstreamerAudioClock::syncLevelToText(stat.syncLevel);
    jsonStat["limitRttMax"] = nanosecondsToMicroseconds(stat.limitRttMax);
    jsonStat["limitRttStdDevMax"] = toMicroseconds(rttStdDevMax_);
    jsonStat["limitSyncTimeout"] = toMicroseconds(syncTimeout_);
    jsonStat["syncTimeout"] = nanosecondsToMicroseconds(stat.syncTimeout);
    jsonStat["statRttAvg"] = nanosecondsToMicroseconds(stat.statRttAvg);
    jsonStat["statRttAverage"] = nanosecondsToMicroseconds(stat.statRttAverage);
    jsonStat["statRttMedian"] = nanosecondsToMicroseconds(stat.statRttMedian);
    jsonStat["statRttStdDev"] = nanosecondsToMicroseconds(stat.statRttStdDev);
    if (stat.rateDenom) {
        jsonStat["rate"] = static_cast<double>(stat.rateNum) / static_cast<double>(stat.rateDenom);
    }

    return ClockSyncParams{
        .deviceId = std::string(it->second->deviceId()),
        .peer = it->first,
        .netClockId = std::string(it->second->clockId()),
        .localClock = localClock,
        .netClock = netClock,
        .diff = localClock - netClock,
        .statistics = std::move(jsonStat)};
}
