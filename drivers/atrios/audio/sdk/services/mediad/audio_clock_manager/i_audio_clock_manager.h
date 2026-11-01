#pragma once

#include <yandex_io/libs/audio_player/base/audio_clock.h>

#include <json/json.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quasar {

    class IAudioClockManager {
    public:
        constexpr static std::chrono::microseconds DEFAULT_RTT_MAX{25 * 1000};
        constexpr static std::chrono::microseconds DEFAULT_RTT_STD_DEV_MAX{7 * 1000};
        constexpr static std::chrono::microseconds DEFAULT_SYNC_TIMEOUT_MAX{15000 * 1000};
        constexpr static std::chrono::microseconds DEFAULT_MIN_UPDATE_INTERVAL{0};

        struct ClockSyncParams {
            std::string deviceId;
            std::string peer;
            std::string netClockId;
            int64_t localClock{0};
            int64_t netClock{0};
            int64_t diff{0};

            Json::Value statistics;
        };

        virtual ~IAudioClockManager() = default;
        virtual std::shared_ptr<const AudioClock> localAudioClock() const noexcept = 0;
        virtual std::shared_ptr<const AudioClock> netAudioClock(std::string deviceId, std::string host, int port, std::string clockId) const = 0;
        virtual std::shared_ptr<const AudioClock> netAudioClock(std::string_view clockId) const = 0;
        virtual std::vector<std::shared_ptr<const AudioClock>> allNetAudioClocks() const noexcept = 0;
        virtual int netAudioClockPort() const noexcept = 0;

        virtual std::string_view localClockId() const noexcept = 0;
        virtual std::optional<ClockSyncParams> clockSyncParams(std::string_view clockId) const noexcept = 0;
    };

} // namespace quasar
