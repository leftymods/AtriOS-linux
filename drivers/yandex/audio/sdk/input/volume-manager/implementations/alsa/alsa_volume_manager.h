#pragma once

#include <yandex_io/modules/volume_manager/base/volume_manager.h>

#include <yandex_io/sdk/sdk_interface.h>

#include <alsa/asoundlib.h>

#include <optional>

/**
 * Volume manager which sets volume through alsa. Is abstract, should be implemented:
 * 1. Some volume
 * 2. Platform to alsa volume scaling
 */
class AlsaVolumeManager: public VolumeManager {
public:
    AlsaVolumeManager(std::shared_ptr<YandexIO::IDevice> device,
                      std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory,
                      std::shared_ptr<YandexIO::SDKInterface> sdk,
                      const std::string& currentVolumeFilename,
                      const std::string& currentMuteStateFilename,
                      std::optional<int> minVolume = std::nullopt,
                      std::optional<int> maxVolume = std::nullopt,
                      const std::string& card = "default",
                      const std::string& selem = "Master");

    ~AlsaVolumeManager();

protected:
    virtual int platformToAlsa(int platformValue) = 0;
    virtual void onSetVolumeCb(int /* platformValue */) {
    }

    void setVolumeImplementation(int platformVolume) override;
    void initAlsa(std::optional<int> minVolume, std::optional<int> maxVolume, const std::string& card, const std::string& selem);

    long minVolume_;
    long maxVolume_;

    snd_mixer_elem_t* selem_;
    snd_mixer_t* mixer_;
};
