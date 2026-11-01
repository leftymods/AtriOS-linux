#include "spectrum_provider.h"

using namespace YandexIO;
using namespace quasar;

SpectrumProvider::SpectrumProvider(const std::shared_ptr<quasar::ipc::IIpcFactory>& factory)
    : deviceContext_(factory, [this]() {
        std::unique_lock guard(mutex_);
        if (activeMusicPlayer_.has_value()) {
            proto::IOEvent::Spectrum event;
            event.set_player_instance_id(*activeMusicPlayer_);
            guard.unlock();
            event.set_event(proto::IOEvent::Spectrum::START);
            event.set_type(proto::IOEvent::Spectrum::MUSIC);
            deviceContext_.fireSpectrum(std::move(event));
        }
    }, false /* autoConnect */)
{
    deviceContext_.connectToSDK();
}

void SpectrumProvider::provideEvent(proto::IOEvent::Spectrum event) {
    std::unique_lock guard(mutex_);
    if (event.type() == proto::IOEvent::Spectrum::VOICE && event.event() != proto::IOEvent::Spectrum::SPECTRUM) {
        if (event.event() == proto::IOEvent::Spectrum::START || event.event() == proto::IOEvent::Spectrum::RESUME) {
            activeVoicePlayer_ = event.player_instance_id();
        } else if (event.event() == proto::IOEvent::Spectrum::PAUSE) {
            activeVoicePlayer_.reset();
        }
    } else if (event.type() == proto::IOEvent::Spectrum::MUSIC && event.event() != proto::IOEvent::Spectrum::SPECTRUM) {
        if (event.event() == proto::IOEvent::Spectrum::START || event.event() == proto::IOEvent::Spectrum::RESUME) {
            activeMusicPlayer_ = event.player_instance_id();
        } else if (event.event() == proto::IOEvent::Spectrum::PAUSE) {
            activeMusicPlayer_.reset();
        }
    }

    if (event.type() == proto::IOEvent::Spectrum::MUSIC && event.event() == proto::IOEvent::Spectrum::SPECTRUM && activeVoicePlayer_.has_value()) {
        return;
    }

    guard.unlock();
    deviceContext_.fireSpectrum(std::move(event));
}
