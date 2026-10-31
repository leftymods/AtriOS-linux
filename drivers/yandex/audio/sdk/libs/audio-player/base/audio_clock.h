#pragma once

#include <chrono>
#include <string_view>
#include <typeinfo>

namespace quasar {

    class AudioClock {
    public:
        enum class SyncLevel {
            NONE,
            WEAK,
            STRONG,
        };

        virtual ~AudioClock() = default;
        virtual std::string_view deviceId() const noexcept = 0;
        virtual std::string_view host() const noexcept = 0;
        virtual int port() const noexcept = 0;
        virtual std::string_view clockId() const noexcept = 0;
        virtual std::chrono::nanoseconds now() const noexcept = 0;
        virtual SyncLevel waitForSync(std::chrono::milliseconds timeout) const noexcept = 0;
        virtual SyncLevel syncLevel() const noexcept = 0;

        static const char* syncLevelToText(AudioClock::SyncLevel syncLevel);
    };

} // namespace quasar
