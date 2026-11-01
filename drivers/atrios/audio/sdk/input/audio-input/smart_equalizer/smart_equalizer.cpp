#include "smart_equalizer.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <voicetech/vqe/yandex_vqe/c/yandex_vqe_cxx_api.h>

#include <util/generic/yexception.h>

namespace YandexIO {
    namespace {
        void convertToFloat(const std::vector<int16_t>& inVec, std::vector<float>& outVec) {
            static constexpr float scale = 32768.f;
            outVec.resize(inVec.size());
            for (size_t i = 0; i < inVec.size(); ++i) {
                outVec[i] = inVec[i] / scale;
            }
        }

        inline EqualizerConfig::Band::Type convertEqualizerBandType(YandexVqeC_EqualizerBandType bandType) {
            switch (bandType) {
                default:
                    YIO_LOG_WARN("Unknown equalizer band type got from Smart Equalizer: " << (int)bandType);
                case YAVQE_EQ_BANDTYPE_PEAK:
                    return EqualizerConfig::Band::Type::PEAK;
                case YAVQE_EQ_BANDTYPE_LOW_SHELF:
                    return EqualizerConfig::Band::Type::LOW_SHELF;
                case YAVQE_EQ_BANDTYPE_HIGH_SHELF:
                    return EqualizerConfig::Band::Type::HIGH_SHELF;
            }
        }

    } // namespace

    class SmartEqualizer::Impl {
    public:
        explicit Impl(const std::string& preset)
            : smartEqualizer_(preset)
        {
        }

        void processAudioData(const ChannelsData& data) {
            bool noMic = true;
            bool noSpk = true;
            for (const auto& channel : data) {
                if (channel.name == "raw_il_mic") {
                    convertToFloat(channel.data, micInterleaved_);
                    noMic = false;
                } else if (channel.name == "raw_il_spk") {
                    convertToFloat(channel.data, spkInterleaved_);
                    noSpk = false;
                }
            }

            if (noMic || noSpk) {
                YIO_LOG_WARN("Empty interleaved spk/mic from audio source");
                return;
            }

            smartEqualizer_.process(micInterleaved_, spkInterleaved_);
        }

        std::optional<EqualizerConfig> getConfig() {
            smartEqualizer_.getEqualizerConfig(equalizerConfig_);
            const auto nBands = equalizerConfig_.size();
            if (!nBands.has_value()) {
                return {};
            }

            EqualizerConfig newConfig;
            newConfig.bands.resize(nBands.value());

            for (size_t i = 0; i < newConfig.bands.size(); ++i) {
                float minFreq;
                float maxFreq;
                float gain;
                YandexVqeC_EqualizerBandType bandType;
                equalizerConfig_.getBandSettings(i, minFreq, maxFreq, gain, bandType);

                auto& configBand = newConfig.bands[i];
                configBand.freq = (minFreq + maxFreq) / 2.;
                configBand.width = maxFreq - minFreq;
                configBand.gain = gain;
                configBand.type = convertEqualizerBandType(bandType);
            }

            return newConfig;
        }

        void onOmniMicChange(int omniMic) {
            smartEqualizer_.onOmniMicChange(omniMic);
        }

        void setUserEqualizerConfig(const EqualizerConfig& config) {
            equalizerConfig_.setSize(config.bands.size());
            for (size_t i = 0; i < config.bands.size(); ++i) {
                auto& configBand = config.bands[i];
                const float halfWidth = configBand.width * 0.5;
                equalizerConfig_.setBandSettings(i, configBand.freq - halfWidth, configBand.freq + halfWidth, configBand.gain);
            }
            smartEqualizer_.setUserEqualizerConfig(equalizerConfig_);
        }

        SmartEqualizer::Rms getMicAndFeedbackRms() {
            const auto pair = smartEqualizer_.getMicAndFeedbackRms();
            return SmartEqualizer::Rms{
                .mic = pair.first,
                .feedback = pair.second};
        }

        std::string toString() {
            return smartEqualizer_.toString();
        }

    private:
        vqe::SmartEqualizerC smartEqualizer_;
        vqe::EqualizerConfigC equalizerConfig_;

        std::vector<float> micInterleaved_;
        std::vector<float> spkInterleaved_;
    };

    SmartEqualizer::SmartEqualizer(const Json::Value& config)
        : defaultConfig_(config)
    {
        applyConfig(defaultConfig_);
    }

    SmartEqualizer::~SmartEqualizer() = default;

    void SmartEqualizer::setConfig(const Json::Value& config) {
        applyConfig(config);
    }

    bool SmartEqualizer::initialized() const {
        return impl_ != nullptr;
    }

    void SmartEqualizer::processAudioData(const ChannelsData& data) {
        if (impl_ != nullptr) {
            impl_->processAudioData(data);
        }
    }

    std::optional<EqualizerConfig> SmartEqualizer::getConfig() {
        return impl_ == nullptr ? std::nullopt : impl_->getConfig();
    }

    void SmartEqualizer::onOmniMicChange(int omniMic) {
        if (impl_ != nullptr) {
            impl_->onOmniMicChange(omniMic);
        }
    }

    void SmartEqualizer::setUserEqualizerConfig(const EqualizerConfig& config) {
        if (impl_ != nullptr) {
            impl_->setUserEqualizerConfig(config);
        }
    }

    SmartEqualizer::Rms SmartEqualizer::getMicAndFeedbackRms() {
        if (impl_ != nullptr) {
            impl_->getMicAndFeedbackRms();
        }
        return {0., 0.};
    }

    std::string SmartEqualizer::toString() {
        if (impl_ != nullptr) {
            return impl_->toString();
        }
        return {};
    }

    void SmartEqualizer::applyConfig(const Json::Value& config) {
        const auto preset = quasar::tryGetString(config, "preset");

        try {
            Y_ENSURE(!preset.empty());
            impl_ = std::make_unique<SmartEqualizer::Impl>(preset);
            YIO_LOG_INFO("Smart equalizer with preset: " << preset << " created");
        } catch (const std::exception& err) {
            YIO_LOG_WARN("Cannot create smart equalizer: " << err.what());

            if (config != defaultConfig_) {
                YIO_LOG_INFO("Fallback to default config");
                applyConfig(defaultConfig_);
                return;
            }

            impl_.reset();
        }
    }

} // namespace YandexIO
