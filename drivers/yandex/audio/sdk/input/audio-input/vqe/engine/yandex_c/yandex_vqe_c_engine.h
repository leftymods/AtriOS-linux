#pragma once

#include <yandex_io/modules/audio_input/vqe/engine/vqe_engine.h>

#include <yandex_io/libs/audio/reader/audio_reader_config.h>
#include <yandex_io/libs/telemetry/telemetry.h>

#include <voicetech/vqe/yandex_vqe/c/yandex_vqe_cxx_api.h>

#include <memory>

namespace YandexIO {
    class YandexVQECEngine: public VQEEngine {
    public:
        static std::shared_ptr<YandexVQECEngine> create(const quasar::AudioReaderConfig& config, std::shared_ptr<YandexIO::ITelemetry> telemetry);

        YandexVQECEngine(quasar::AudioReaderConfig config, std::shared_ptr<YandexIO::ITelemetry> telemetry);
        ~YandexVQECEngine();
        void process(const std::vector<float>& inputMic,
                     const std::vector<float>& inputSpk,
                     double& doaAngle,
                     bool& speechDetected) override;

        ChannelCount getInputChannelCount() const override;
        size_t getOutputChannelCount(ChannelData::Type chType) const override;
        std::span<const float> getOutputChannelData(ChannelData::Type chType, size_t channelId) const override;

        void setOmniMode(bool omniMode) override;

        int getPeriodSize() const override;
        std::optional<int> getFeedbackShift() const override;
        std::optional<float> getFeedbackShiftCorrelation() const override;

        void setSpeakerVolume(int volume) override;

        FeedbackSource hardwareSyncTarget() const override;

    private:
        const std::shared_ptr<YandexIO::ITelemetry> telemetry_;
        const quasar::AudioReaderConfig arConfig_;

        vqe::YandexVqeCConfig vqeConfig_;
        vqe::YandexVqeC vqe_;

        int omniMicIndex_;
        vqe::YandexVqeC_CorrelationResult micSpeakShifts_;
    };
} // namespace YandexIO
