#include "alsa_volume_manager.h"

#include <yandex_io/libs/audio/alsa/alsa_error.h>
#include <yandex_io/libs/logging/logging.h>

#include <util/generic/scope.h>

#include <cmath>
#include <string_view>

namespace {

    void checkAlsaReturn(int returnCode, std::string_view functionName) {
        if (returnCode) {
            std::stringstream ss;
            ss << "Function " << functionName << " returned non-zero code: " << quasar::alsaErrorTextMessage(returnCode);
            throw std::runtime_error(ss.str());
        }
    }

} // namespace

AlsaVolumeManager::AlsaVolumeManager(std::shared_ptr<YandexIO::IDevice> device,
                                     std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory,
                                     std::shared_ptr<YandexIO::SDKInterface> sdk,
                                     const std::string& currentVolumeFilename,
                                     const std::string& currentMuteStateFilename,
                                     std::optional<int> minVolume,
                                     std::optional<int> maxVolume,
                                     const std::string& card,
                                     const std::string& selem)
    : VolumeManager(std::move(device), std::move(ipcFactory), std::move(sdk), currentVolumeFilename, currentMuteStateFilename)
{
    initAlsa(minVolume, maxVolume, card, selem);
}

void AlsaVolumeManager::initAlsa(std::optional<int> minVolume, std::optional<int> maxVolume, const std::string& card, const std::string& selem) {
    snd_mixer_selem_id_t* sid;

    bool initializationSuccess = false;
    checkAlsaReturn(snd_mixer_open(&mixer_, 0), "snd_mixer_open");
    Y_DEFER {
        if (!initializationSuccess) {
            snd_mixer_close(mixer_);
        }
    };
    checkAlsaReturn(snd_mixer_attach(mixer_, card.c_str()), "snd_mixer_attach");
    checkAlsaReturn(snd_mixer_selem_register(mixer_, nullptr, nullptr), "snd_mixer_selem_register");
    checkAlsaReturn(snd_mixer_load(mixer_), "snd_mixer_load");
    Y_DEFER {
        if (!initializationSuccess) {
            snd_mixer_free(mixer_);
        }
    };

    snd_mixer_selem_id_alloca(&sid);
    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, selem.c_str());
    selem_ = snd_mixer_find_selem(mixer_, sid);
    checkAlsaReturn(snd_mixer_selem_get_playback_volume_range(selem_, &minVolume_, &maxVolume_), "snd_mixer_selem_get_playback_volume_range");

    // Change min-max volume borders, if specified
    if (minVolume) {
        if (*minVolume < minVolume_) {
            std::stringstream ss;
            ss << "Min volume threshold is incorrect " << *minVolume << ", but it should be not less than " << minVolume_;
            throw std::runtime_error(ss.str());
        } else {
            minVolume_ = *minVolume;
        }
    }

    if (maxVolume) {
        if (*maxVolume > maxVolume_) {
            std::stringstream ss;
            ss << "Max volume threshold is incorrect " << *maxVolume << ", but it should be not more than " << maxVolume_;
            throw std::runtime_error(ss.str());
        } else {
            maxVolume_ = *maxVolume;
        }
    }

    initializationSuccess = true;
}

AlsaVolumeManager::~AlsaVolumeManager() {
    snd_mixer_free(mixer_);
    snd_mixer_close(mixer_);
}

void AlsaVolumeManager::setVolumeImplementation(int platformVolume) {
    int alsaVolume = platformToAlsa(platformVolume);
    YIO_LOG_DEBUG("Setting volume " << alsaVolume << " with ALSA mixer");
    checkAlsaReturn(snd_mixer_selem_set_playback_volume_all(selem_, (int)alsaVolume), "snd_mixer_selem_set_playback_volume_all");

    onSetVolumeCb(platformVolume);
}
