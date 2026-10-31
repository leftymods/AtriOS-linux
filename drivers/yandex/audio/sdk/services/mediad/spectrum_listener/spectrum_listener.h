#pragma once

#include "spectrum_provider.h"

#include <yandex_io/libs/audio_player/base/audio_player.h>

namespace quasar {

    class SpectrumListener: public AudioPlayer::SimpleListener {
    public:
        enum class SpectrumType {
            MUSIC,
            VOICE,
        };

        SpectrumListener(std::shared_ptr<YandexIO::SpectrumProvider> provider, SpectrumType type = SpectrumType::MUSIC);
        void onPaused() override;
        void onResumed() override;
        void onStart() override;
        void onSpectrum(const SpectrumFrame& frame) override;
        void onEnd() override;

    private:
        void handleEvent(const proto::IOEvent::Spectrum::Event& event);

    private:
        const std::shared_ptr<YandexIO::SpectrumProvider> provider_;
        int64_t instanceId_{0};
        proto::IOEvent::Spectrum::Type type_;
    };

} // namespace quasar
