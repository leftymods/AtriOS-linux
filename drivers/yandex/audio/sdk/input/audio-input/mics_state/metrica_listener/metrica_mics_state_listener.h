#pragma once

#include <yandex_io/modules/audio_input/mics_state/i_mics_state_listener.h>

#include <yandex_io/libs/telemetry/telemetry.h>

#include <functional>
#include <memory>

namespace YandexIO {
    /**
     * @brief Simple listener that set up "areMicsMuted" environment variable (1 - muted, 0 - not muted)
     *        and send muteMics/unmuteMics events when mics state changes
     */
    class MetricaMicsStateListener: public IMicsStateListener {
    public:
        explicit MetricaMicsStateListener(std::shared_ptr<YandexIO::ITelemetry> telemetry);

        void onMuted() override;
        void onUnmuted() override;

    private:
        std::shared_ptr<YandexIO::ITelemetry> telemetry_;
    };

} /* namespace YandexIO */
