#include "yandex_vqe_c_engine.h"

#include <yandex_io/libs/audio/common/defines.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <yandex_io/sdk/audio_source/feedback_source.h>

#include <voicetech/library/channel_types/channel_types.h>

#include <util/string/cast.h>

YIO_DEFINE_LOG_MODULE("vqe");

#define ASSERT_EQUAL(A, B)                                                                                                                 \
    {                                                                                                                                      \
        auto __a = (A);                                                                                                                    \
        auto __b = (B);                                                                                                                    \
        if (!(__a == __b))                                                                                                                 \
            throw std::runtime_error("Runtime assertion failed: " #A " == " #B ", " + std::to_string(__a) + " != " + std::to_string(__b)); \
    }

namespace {
    void vqeLogHandler(char logPriority, const char* message) {
        spdlog::level::level_enum spdLogLevel;

        switch (logPriority) {
            default:
                YIO_LOG_ERROR_EVENT("YandexVQECEngine.UnknownLogPriority",
                                    "Unknown logPriority \"" << logPriority << "\" for log \"" << message << '\"');
                return;
            case 'E':
                YIO_LOG_ERROR_EVENT("YandexVQECEngine.InternalError", message);
                return;
            case 'T':
                spdLogLevel = spdlog::level::trace;
                break;
            case 'D':
                spdLogLevel = spdlog::level::debug;
                break;
            case 'I':
                spdLogLevel = spdlog::level::info;
                break;
            case 'W':
                spdLogLevel = spdlog::level::warn;
        }

        YIO_SPDLOG_LOGGER_CALL(spdLogLevel, message);
    }

    vqe::YandexVqeCConfig makeVqeConfig(const quasar::AudioReaderConfig& arConfig) {
        const std::string preset = arConfig.preset;
        YIO_LOG_INFO("Yandex VQE preset = '" << preset << "'");
        auto vqeConfig = [&]() {
            auto jsonStartPos = preset.find('@');
            if (jsonStartPos == std::string::npos) {
                return vqe::YandexVqeCConfig::fromPreset(preset.c_str());
            }
            auto deviceName = preset.substr(0, jsonStartPos);
            auto rawJson = preset.substr(jsonStartPos + 1);
            return vqe::YandexVqeCConfig::fromJsonPreset(deviceName.c_str(), rawJson.c_str());
        }();

        ASSERT_EQUAL(vqeConfig.processingFrame(), arConfig.periodSize);
        ASSERT_EQUAL(vqeConfig.micsCount(), arConfig.micChannels);
        ASSERT_EQUAL(vqeConfig.speakersCount(), arConfig.spkChannels);
        ASSERT_EQUAL(vqeConfig.outputSamplingRate(), DEFAULT_AUDIO_SAMPLING_RATE);
        ASSERT_EQUAL(vqeConfig.inputSamplingRate(), arConfig.inRate);

        return vqeConfig;
    }

    std::string makeOmniMicChangedPayload(int oldMic, int newMic, std::span<const float> health) {
        Json::Value result;
        result["oldMic"] = oldMic;
        result["newMic"] = newMic;
        result["health"] = quasar::vectorToJson(health);
        return quasar::jsonToString(result);
    }

    YandexIO::FeedbackSource convertYaVqeSpeakerSource(YandexVqeC_SpeakerSource yaVqeSrc) {
        switch (yaVqeSrc) {
            case YAVQE_SPEAKER_SOURCE_HW:
                return YandexIO::FeedbackSource::HW;
            case YAVQE_SPEAKER_SOURCE_SW:
                return YandexIO::FeedbackSource::SW;
            case YAVQE_SPEAKER_SOURCE_HDMI:
                return YandexIO::FeedbackSource::HDMI;
            default:
                Y_ENSURE(false, "Unsupported source " << static_cast<int>(yaVqeSrc));
        };
    }

} // namespace

using namespace quasar;

namespace YandexIO {

    std::shared_ptr<YandexVQECEngine> YandexVQECEngine::create(const quasar::AudioReaderConfig& arConfig, std::shared_ptr<YandexIO::ITelemetry> telemetry) {
        return std::make_shared<YandexVQECEngine>(arConfig, std::move(telemetry));
    }

    YandexVQECEngine::YandexVQECEngine(AudioReaderConfig arConfig, std::shared_ptr<YandexIO::ITelemetry> telemetry)
        : telemetry_(std::move(telemetry))
        , arConfig_(std::move(arConfig))
        , vqeConfig_(makeVqeConfig(arConfig_))
        , vqe_(vqeConfig_)
        , omniMicIndex_(vqe_.getOmniMicIndex())
    {
        YIO_LOG_INFO("Initialized YandexVqe (preset hash = " << vqeConfig_.toHash() << ")");
        YIO_LOG_INFO("YandexVqe omni microphone index: " << omniMicIndex_);
        YIO_LOG_INFO("YandexVqe full config: " << vqeConfig_.toString(/* prettify = */ false));
        vqe::logging::setLoggerHandler(vqeLogHandler);
    }

    YandexVQECEngine::~YandexVQECEngine() = default;

    void YandexVQECEngine::process(const std::vector<float>& inputMic, const std::vector<float>& inputSpk,
                                   double& doaAngle, bool& speechDetected)
    {
        vqe_.process(inputMic, inputSpk);

        doaAngle = vqe_.getDoa();
        speechDetected = false;

        if (const auto curOmniMic = vqe_.getOmniMicIndex(); omniMicIndex_ != curOmniMic) {
            const auto healthConfidences = vqe_.getMicHealthConfidences();
            YIO_LOG_DEBUG("Switched YandexVqe omni microphone index from " << omniMicIndex_ << " to " << std::to_string(curOmniMic));
            YIO_LOG_DEBUG("Microphone health confidences: [" << quasar::join(healthConfidences, ", ") << "]");
            telemetry_->reportEvent("yandexVqeOmniMicChanged", makeOmniMicChangedPayload(omniMicIndex_, curOmniMic, healthConfidences));
            omniMicIndex_ = curOmniMic;
        }

        const auto curFeedbackShift = vqe_.getMicSpeakShifts();
        for (const auto& [src, newShift] : curFeedbackShift.shifts) {
            auto oldShiftIter = micSpeakShifts_.shifts.find(src);
            if (oldShiftIter == micSpeakShifts_.shifts.end()) {
                YIO_LOG_INFO("YandexVqe changed feedback shift on source " << ToString(convertYaVqeSpeakerSource(src))
                                                                           << " from none to " << newShift);
                micSpeakShifts_.shifts[src] = newShift;
            } else if (oldShiftIter->second != newShift) {
                YIO_LOG_INFO("YandexVqe changed feedback shift on source " << ToString(convertYaVqeSpeakerSource(src)) << " from "
                                                                           << oldShiftIter->second << " to " << newShift);
                oldShiftIter->second = newShift;
            }
        }
    }

    YandexVQECEngine::ChannelCount YandexVQECEngine::getInputChannelCount() const {
        return {static_cast<size_t>(vqeConfig_.micsCount()), static_cast<size_t>(vqeConfig_.speakersCount())};
    }

    static inline AudioInputChannelType speechKitChannelToYandexVqeChannelType(ChannelData::Type chType) {
        switch (chType) {
            case ChannelData::Type::VQE:
                return AUDIO_INPUT_CHANNEL_TYPE_VQE_OMNI;
            case ChannelData::Type::BEAMFORMING:
                return AUDIO_INPUT_CHANNEL_TYPE_VQE_BEAMFORMING;
            case ChannelData::Type::BACKGROUND_NOISE_REDUCER:
                return AUDIO_INPUT_CHANNEL_TYPE_VQE_BACKGROUND_NOISE_REDUCER;
            case ChannelData::Type::MAIN_MIC_SYNC:
                return AUDIO_INPUT_CHANNEL_TYPE_RAW_MAIN_MIC_SYNC;
            case ChannelData::Type::AUXILIARY_MIC_SYNC:
                return AUDIO_INPUT_CHANNEL_TYPE_RAW_AUXILIARY_MIC_SYNC;
            case ChannelData::Type::FEEDBACK_SYNC:
                return AUDIO_INPUT_CHANNEL_TYPE_RAW_FEEDBACK_SYNC;
            default:
                throw std::runtime_error("Channel type not supported by Yandex VQE requested");
        }
    }

    size_t YandexVQECEngine::getOutputChannelCount(ChannelData::Type chType) const {
        return vqeConfig_.getOutputChannelCount(speechKitChannelToYandexVqeChannelType(chType));
    }

    std::span<const float> YandexVQECEngine::getOutputChannelData(ChannelData::Type chType, size_t channelId) const {
        return vqe_.getOutputChannelData(speechKitChannelToYandexVqeChannelType(chType), channelId);
    }

    void YandexVQECEngine::setOmniMode(bool omniMode) {
        vqe_.setASRMode(!omniMode);
    }

    int YandexVQECEngine::getPeriodSize() const {
        return vqeConfig_.inputChunkSize();
    }

    std::optional<int> YandexVQECEngine::getFeedbackShift() const {
        const auto hwShiftIter = micSpeakShifts_.shifts.find(YAVQE_SPEAKER_SOURCE_HW);
        if (hwShiftIter != micSpeakShifts_.shifts.end()) {
            return hwShiftIter->second;
        } else {
            return std::nullopt;
        }
    }

    std::optional<float> YandexVQECEngine::getFeedbackShiftCorrelation() const {
        return std::nullopt;
    }

    void YandexVQECEngine::setSpeakerVolume(int volume)
    {
        vqe_.setSpeakerVolume(volume);
        YIO_LOG_INFO("YandexVqe changed volume  to " << volume);
    }

    FeedbackSource YandexVQECEngine::hardwareSyncTarget() const {
        return convertYaVqeSpeakerSource(vqe_.hardwareSyncTarget());
    }

} // namespace YandexIO
