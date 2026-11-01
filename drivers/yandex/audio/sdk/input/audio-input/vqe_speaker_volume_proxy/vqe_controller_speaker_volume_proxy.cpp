#include "vqe_controller_speaker_volume_proxy.h"

VqeControllerSpeakerVolumeProxy::VqeControllerSpeakerVolumeProxy(std::weak_ptr<YandexIO::VqeController> controller)
    : controller_(std::move(controller))
{
}

void VqeControllerSpeakerVolumeProxy::onVolumeChange(
    int /* platformVolume */, int aliceVolume,
    bool /* isMuted */, const std::string& /* source */,
    bool /* setBtVolume */) {
    if (auto scontroller = controller_.lock()) {
        scontroller->setSpeakerVolume(aliceVolume);
    }
}
