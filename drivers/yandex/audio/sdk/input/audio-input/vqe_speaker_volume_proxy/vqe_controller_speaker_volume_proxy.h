#pragma once

#include <yandex_io/modules/audio_input/vqe/controller/vqe_controller.h>

#include <yandex_io/modules/volume_manager/base/i_volume_manager_listener.h>

#include <memory>

class VqeControllerSpeakerVolumeProxy: public IVolumeManagerListener {
public:
    explicit VqeControllerSpeakerVolumeProxy(std::weak_ptr<YandexIO::VqeController> controller);

    void onVolumeChange(int platformVolume, int aliceVolume, bool isMuted, const std::string& source, bool setBtVolume) override;

private:
    std::weak_ptr<YandexIO::VqeController> controller_;
};
