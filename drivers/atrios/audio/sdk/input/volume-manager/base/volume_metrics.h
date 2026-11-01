#pragma once

#include "i_volume_manager_listener.h"

#include <yandex_io/libs/device/device.h>

#include <memory>
#include <optional>

/**
 * @brief VolumeMetrics sends analytic events to ITelemetry when volume is changed
 */
class VolumeMetrics: public IVolumeManagerListener {
public:
    explicit VolumeMetrics(std::shared_ptr<YandexIO::ITelemetry> telemetry);
    void onVolumeChange(int platformVolume, int aliceVolume, bool isMuted, const std::string& source, bool setBtVolume) override;

private:
    const std::shared_ptr<YandexIO::ITelemetry> telemetry_;
    std::optional<int> aliceVolume_;
    std::optional<bool> muted_;
};
