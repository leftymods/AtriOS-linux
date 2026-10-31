#include "spectrum_listener.h"

#include <yandex_io/libs/logging/logging.h>

#include <chrono>

using namespace quasar;

SpectrumListener::SpectrumListener(std::shared_ptr<YandexIO::SpectrumProvider> provider, SpectrumType type)
    : provider_(std::move(provider))
    , instanceId_(std::chrono::steady_clock::now().time_since_epoch().count())
{
    switch (type) {
        case SpectrumListener::SpectrumType::MUSIC: {
            type_ = proto::IOEvent::Spectrum::MUSIC;
            break;
        }
        case SpectrumListener::SpectrumType::VOICE: {
            type_ = proto::IOEvent::Spectrum::VOICE;
            break;
        }
    }
}

void SpectrumListener::onSpectrum(const SpectrumFrame& frame) {
    proto::IOEvent::Spectrum spectrum;

    spectrum.set_player_instance_id(instanceId_);
    spectrum.set_event(proto::IOEvent::Spectrum::SPECTRUM);
    spectrum.set_type(type_);
    spectrum.set_running_time_ns(frame.runningTime.count());
    spectrum.set_duration_ns(frame.duration.count());
    spectrum.set_rate(frame.rate);
    for (float mag : frame.magnitudes) {
        spectrum.add_magnitudes(mag);
    }
    spectrum.set_threshold(frame.threshold);

    provider_->provideEvent(std::move(spectrum));
}

void SpectrumListener::onStart() {
    handleEvent(proto::IOEvent::Spectrum::START);
}

void SpectrumListener::onPaused() {
    handleEvent(proto::IOEvent::Spectrum::PAUSE);
}

void SpectrumListener::onResumed() {
    handleEvent(proto::IOEvent::Spectrum::RESUME);
}

void SpectrumListener::onEnd() {
    handleEvent(proto::IOEvent::Spectrum::PAUSE);
}

void SpectrumListener::handleEvent(const proto::IOEvent::Spectrum::Event& event) {
    proto::IOEvent::Spectrum spectrum;

    spectrum.set_player_instance_id(instanceId_);
    spectrum.set_event(event);
    spectrum.set_type(type_);

    provider_->provideEvent(std::move(spectrum));
}
