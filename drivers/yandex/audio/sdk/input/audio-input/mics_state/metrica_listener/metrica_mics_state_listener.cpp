#include "metrica_mics_state_listener.h"

#include <util/system/yassert.h>

namespace YandexIO {

    MetricaMicsStateListener::MetricaMicsStateListener(std::shared_ptr<YandexIO::ITelemetry> telemetry)
        : telemetry_(std::move(telemetry))
    {
        Y_VERIFY(telemetry_ != nullptr);
    }

    void MetricaMicsStateListener::onMuted() {
        telemetry_->reportEvent("muteMics");
        telemetry_->putAppEnvironmentValue("areMicsMuted", "1");
    }

    void MetricaMicsStateListener::onUnmuted() {
        telemetry_->reportEvent("unmuteMics");
        telemetry_->putAppEnvironmentValue("areMicsMuted", "0");
    }

} /* namespace YandexIO */
