#include "audio_sender_service.h"

using namespace quasar;

AudioSenderService::AudioSenderService(
    std::shared_ptr<YandexIO::IDevice> device,
    const std::shared_ptr<ipc::IIpcFactory>& ipcFactory,
    SpeechKit::AudioPlayer::SharedPtr audioPlayer,
    std::shared_ptr<quasar::IAuthProvider> authProvider)
    : endpoint_(std::move(device), ipcFactory, std::move(audioPlayer), std::move(authProvider))
{
}

void AudioSenderService::start() {
    endpoint_.start();
}

void AudioSenderService::stop() {
    endpoint_.stop();
}

std::string AudioSenderService::getServiceName() const {
    return AudioSenderEndpoint::SERVICE_NAME;
}
