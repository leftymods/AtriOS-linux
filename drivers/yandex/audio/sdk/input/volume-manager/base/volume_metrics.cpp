#include "volume_metrics.h"

#include <yandex_io/libs/json_utils/json_utils.h>

using namespace quasar;

VolumeMetrics::VolumeMetrics(std::shared_ptr<YandexIO::ITelemetry> telemetry)
    : telemetry_(std::move(telemetry))
{
}

void VolumeMetrics::onVolumeChange(int /* platformVolume */, int aliceVolume, bool isMuted, const std::string& source, bool /* setBtVolume */) {
    if (!aliceVolume_.has_value() || !muted_.has_value()) {
        aliceVolume_ = aliceVolume;
        muted_ = isMuted;
        telemetry_->putAppEnvironmentValue("currentVolume", isMuted ? "0" : std::to_string(aliceVolume));
        return;
    }
    if (*aliceVolume_ == aliceVolume && *muted_ == isMuted) {
        return;
    }
    Json::Value payload;
    payload["alice_volume"] = aliceVolume;
    payload["prev_alice_volume"] = *aliceVolume_;
    payload["muted"] = isMuted;
    payload["source"] = source;
    telemetry_->reportEvent("volumeChanged", jsonToString(payload));
    telemetry_->putAppEnvironmentValue("currentVolume", isMuted ? "0" : std::to_string(aliceVolume));

    muted_ = isMuted;
    aliceVolume_ = aliceVolume;
}
