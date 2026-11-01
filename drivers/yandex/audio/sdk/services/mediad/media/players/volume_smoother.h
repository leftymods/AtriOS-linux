#pragma once

#include <chrono>
#include <functional>
#include <mutex>

namespace quasar {

    class VolumeSmoother {
    public:
        /**
         * Creates volume controller which smoothly increase / decrease volume
         * @throw quasar::ErrnoException - if failed to create a timer or other error
         * @param currentVolume - initial volume
         * @param clock - steady clock. Can be used for testing purpose
         */
        VolumeSmoother(float currentVolume);
        VolumeSmoother(float currentVolume, std::function<std::chrono::steady_clock::time_point()> clock);

        /**
         * Enables smooth volume transition from current to targetVolume
         * @param targetVolume - final volume
         * @param period - volume change rate from 0 to 1, for example, if the current
         *                 volume is 0.5, then switching to volume 1.0 will require a
         *                 period/2 time
         */
        void setVolume(float targetVolume, std::chrono::milliseconds period);

        /**
         * Return effective volume at this moment
         */
        float currentVolume() const;

        /**
         * Return final volume
         */
        float targetVolume() const;

        /**
         * Return duration until target volume
         */
        std::chrono::milliseconds duration() const;

    private:
        float currentVolumeUnsafe(std::chrono::steady_clock::time_point /*now*/) const;

    private:
        const std::function<std::chrono::steady_clock::time_point()> clock_;
        mutable std::mutex mutex_;
        float startVolume_;
        float targetVolume_;
        std::chrono::steady_clock::time_point startTime_;
        std::chrono::steady_clock::time_point targetTime_;
    };

} // namespace quasar
