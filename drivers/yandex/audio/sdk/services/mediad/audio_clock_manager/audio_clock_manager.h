#pragma once

#include "i_audio_clock_manager.h"

#include <memory>
#include <mutex>
#include <unordered_map>

namespace quasar {

    class GstreamerAudioClock;
    class AudioClockManager: public IAudioClockManager {
    public:
        static std::string generateClockId() noexcept;

        AudioClockManager(std::string deviceId, int netAudioClockPort, std::string clockId);
        std::shared_ptr<const AudioClock> localAudioClock() const noexcept override;
        std::shared_ptr<const AudioClock> netAudioClock(std::string deviceId, std::string host, int port, std::string clockId) const override;
        std::shared_ptr<const AudioClock> netAudioClock(std::string_view clockId) const override;
        std::vector<std::shared_ptr<const AudioClock>> allNetAudioClocks() const noexcept override;
        int netAudioClockPort() const noexcept override;

        std::string_view localClockId() const noexcept override;
        std::optional<ClockSyncParams> clockSyncParams(std::string_view clockId) const noexcept override;

    private:
        const std::string clockId_;
        const int netAudioClockPort_;
        const std::shared_ptr<void> netTimeProvider_;
        const std::shared_ptr<GstreamerAudioClock> localAudioClock_;

        const std::chrono::microseconds rttMax_{DEFAULT_RTT_MAX};
        const std::chrono::microseconds rttStdDevMax_{DEFAULT_RTT_STD_DEV_MAX};
        const std::chrono::microseconds syncTimeout_{DEFAULT_SYNC_TIMEOUT_MAX};
        const std::chrono::microseconds minUpdateInterval_{DEFAULT_MIN_UPDATE_INTERVAL};

        mutable std::mutex mutex_;
        mutable std::unordered_map<std::string, std::shared_ptr<GstreamerAudioClock>> netAudioClocks_;
    };

} // namespace quasar
