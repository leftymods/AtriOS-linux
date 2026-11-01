#include "yandex_vqe_audio_device.h"

#include <yandex_io/modules/audio_input/vqe/controller/yandex_vqe/vqe_controller_with_yandex_vqe.h>
#include <yandex_io/modules/audio_input/vqe/engine/yandex_c/yandex_vqe_c_engine.h>

#include <yandex_io/libs/audio/alsa/alsa_audio_reader.h>
#include <yandex_io/libs/audio/reader/audio_reader_config.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/defines.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <json/value.h>

#include <algorithm>
#include <stdexcept>

YIO_DEFINE_LOG_MODULE("audio_device");

using std::string;
using std::to_string;

using namespace quasar;

namespace {

    constexpr char VQE_CHANNEL_NAME[] = "vqe_0";
    constexpr char RAW_MIC_STREAM_TYPE[] = "raw_mic";
    constexpr char RAW_SPK_STREAM_TYPE[] = "raw_spk";
    constexpr char RAW_IL_MIC_STREAM_TYPE[] = "raw_il_mic";
    constexpr char RAW_IL_SPK_STREAM_TYPE[] = "raw_il_spk";
    constexpr int DEFAULT_VQE_QUEUE_LIMIT = 8;
    constexpr float RAW_CHANNEL_NORMALIZATION_SCALE = 32768.0;

    using namespace YandexIO;

    AudioDevice::ChannelsList makeAvailableChannels(const Json::Value& config,
                                                    const YandexVqeAudioDevice::ChannelsCounters& numberOfChannels)
    {
        const AudioDevice::ChannelInfo info{2u, (unsigned)tryGetInt(config, "outRate", DEFAULT_AUDIO_SAMPLING_RATE)};
        AudioDevice::ChannelsList result;

        for (int i = 0; i < numberOfChannels.mics; i++) {
            result.emplace(string(RAW_MIC_STREAM_TYPE) + "_" + to_string(i), info);
        }
        for (int i = 0; i < numberOfChannels.spks; i++) {
            result.emplace(string(RAW_SPK_STREAM_TYPE) + "_" + to_string(i), info);
        }

        result.emplace(RAW_IL_MIC_STREAM_TYPE, info);
        result.emplace(RAW_IL_SPK_STREAM_TYPE, info);

        return result;
    }

    YandexVqeAudioDevice::ChannelsCounters makeChannelsCounters(const Json::Value& config) {
        YandexVqeAudioDevice::ChannelsCounters rval;
        rval.mics = tryGetInt(config, "micExpectedChannels", DEFAULT_MIC_CHANNELS);
        rval.spks = tryGetInt(config, "spkChannels", DEFAULT_SPK_CHANNELS);
        rval.micsUnknown = tryGetInt(config, "micChannels", rval.mics) - rval.mics;
        rval.total = rval.mics + rval.micsUnknown + rval.spks;
        rval.hasVqe = quasar::tryGetBool(config, "yandex_vqe", false);
        return rval;
    }

    bool readAlsa(AudioReader& reader, int size, std::vector<uint8_t>& readBuffer) {
        if (reader.read(readBuffer, size)) {
            return true;
        }
        try {
            YIO_LOG_ERROR_EVENT("YandexVqeAudioDevice.ReadAlsa.Failed", "Audio read returned false; Error: " << reader.getError() << ". Trying to recover");
            reader.tryRecover();
        } catch (const std::runtime_error& e) {
            YIO_LOG_ERROR_EVENT("YandexVqeAudioDevice.AlsaRecover.Failed", "AudioReader error with unsuccessful recover: " << e.what());
        }
        return false;
    }
} // namespace

namespace YandexIO {

    YandexVqeAudioDevice::YandexVqeAudioDevice(const Json::Value& config,
                                               const std::string& deviceType,
                                               std::shared_ptr<ITelemetry> telemetry)
        : YandexVqeAudioDevice(AudioReaderConfig(config, deviceType),
                               deviceType,
                               makeChannelsCounters(config),
                               config,
                               std::move(telemetry))
              {};

    YandexVqeAudioDevice::YandexVqeAudioDevice(const AudioReaderConfig& arConfig,
                                               const std::string& deviceType,
                                               const ChannelsCounters& numberOfChannels,
                                               const Json::Value& config,
                                               std::shared_ptr<ITelemetry> telemetry)
        : YandexVqeAudioDeviceBase(numberOfChannels, VQE_CHANNEL_NAME, 0, makeAvailableChannels(config, numberOfChannels),
                                   makeVqeControllerWithYandexVqe(telemetry, deviceType),
                                   config)
        , telemetry_(std::move(telemetry))
        , arConfig_(arConfig)
        , numberOfChannels_(numberOfChannels)
        , frameSize_(numberOfChannels_.total * arConfig_.sampleSize)
        , outRate_(tryGetInt(config, "outRate", DEFAULT_AUDIO_SAMPLING_RATE))
        , splitter_(IChannelSplitter::create(numberOfChannels.mics + numberOfChannels.micsUnknown, numberOfChannels.spks, arConfig_.sampleSize))
        , micDownsampler(arConfig_.inRate, outRate_, numberOfChannels.mics)
        , spkDownsampler(arConfig_.inRate, outRate_, numberOfChannels.spks)
        , perfLogger_(telemetry_)
        , vqeQueueSizeLimit_(tryGetInt(config, "vqeQueueSizeLimit", DEFAULT_VQE_QUEUE_LIMIT))
    {
        if (arConfig_.inRate % outRate_ != 0) {
            throw std::runtime_error("inRate " + to_string(arConfig_.inRate) + " is not divided by outRate " + to_string(outRate_));
        }

        YIO_LOG_INFO("Created YandexVqeAudioDevice with periodSize=" << arConfig_.periodSize
                                                                     << ", sampleSize=" << arConfig_.sampleSize
                                                                     << ", inRate=" << arConfig_.inRate
                                                                     << ", outRate=" << outRate_
                                                                     << ", frameSize=" << frameSize_);

        periodMultiplier_ = arConfig_.inRate / outRate_;
        if (numberOfChannels.hasVqe) {
            vqeDefaultConfig_ = config;
            vqeDefaultConfig_["micChannels"] = numberOfChannels_.mics;
            vqeDefaultConfig_["VQEtype"] = "yandex";
            vqeDefaultConfig_["inRate"] = outRate_;
            /* move vqe parameters into root for arConfig builder
             */
            if (vqeDefaultConfig_.isMember("vqe")) {
                const auto& vqe = vqeDefaultConfig_["vqe"];
                if (vqe.isMember("preset")) {
                    vqeDefaultConfig_["preset"] = vqe["preset"];
                }
            }
            auto vqeArConfig = AudioReaderConfig{vqeDefaultConfig_, deviceType};
            vqeController_->setEngine(std::make_shared<YandexVQECEngine>(vqeArConfig, telemetry_),
                                      vqeArConfig.vqeTypeName, vqeArConfig.preset);
            periodMultiplier_ *= vqeController_->getEngine()->getPeriodSize() / arConfig_.periodSize;
        }
    }

    YandexVqeAudioDevice::~YandexVqeAudioDevice() {
        stop();
    }

    void YandexVqeAudioDevice::onCaptureThreadStart() {
        audioReader_ = createAudioReader(arConfig_);
    }

    void YandexVqeAudioDevice::onCaptureThreadStop() {
        // remove alsa reader so it will not collect data in buffers while muted
        audioReader_.reset();
    }

    double YandexVqeAudioDevice::getDOAAngle() const {
        return 45.;
    }

    void YandexVqeAudioDevice::setASRMode() {
        vqeController_->setOmniMode(false);
    }

    void YandexVqeAudioDevice::setSpotterMode() {
        vqeController_->setOmniMode(true);
    }

    void YandexVqeAudioDevice::addChannelData(const std::vector<float>& buffer, const int channelN, const std::string& channelType)
    {
        for (int i = 0; i < channelN; ++i) {
            const auto channelName = channelType + "_" + to_string(i);
            if (isChannelCaptured(channelName) || vqeController_) {
                convertFPSamplesToInt(channelPerName_[channelName], std::span<const float>(buffer.begin() + i, buffer.end()),
                                      channelN, RAW_CHANNEL_NORMALIZATION_SCALE);
            } else {
                channelPerName_.erase(channelName);
            }
        }
    }

    void YandexVqeAudioDevice::doCapture() {
        auto logSession = perfLogger_.createChunkSession(vqeQueueCounter_.load());

        std::vector<uint8_t> readBuffer;

        logSession->start(AudioDeviceStats::Tag::DO_CAPTURE);

        logSession->start(AudioDeviceStats::Tag::READ_HW_MIC_SPK);
        const bool readSuccess = readAlsa(*audioReader_, arConfig_.periodSize * periodMultiplier_, readBuffer);
        logSession->stop(AudioDeviceStats::Tag::READ_HW_MIC_SPK);

        if (!readSuccess) {
            return;
        }

        if (vqeQueueCounter_ + 1 <= vqeQueueSizeLimit_) {
            if (vqeQueueOverflowed_) {
                YIO_LOG_INFO("VQE queue restored");
                vqeQueueOverflowed_ = false;
            }

            ++vqeQueueCounter_;
            logSession->start(AudioDeviceStats::Tag::ASYNC_LAG);
            worker_.add([this, buffer{std::move(readBuffer)}, logSession{std::move(logSession)}]() mutable {
                logSession->stop(AudioDeviceStats::Tag::ASYNC_LAG);
                logSession->start(AudioDeviceStats::Tag::PROCESSING);
                processData(buffer);
                logSession->stop(AudioDeviceStats::Tag::PROCESSING);
                --vqeQueueCounter_;
            });
        } else if (!vqeQueueOverflowed_) {
            YIO_LOG_ERROR_EVENT("YandexVqeAudioDevice.VqeQueueOverflow", "VQE queue onOverflow");
            vqeQueueOverflowed_ = true;
        }
    }

    void YandexVqeAudioDevice::processData(std::vector<uint8_t>& buffer) {
        YIO_LOG_TRACE("Read " + to_string(buffer.size()) + " bytes from audio reader, periodSize=" + to_string(arConfig_.periodSize) +
                      ", periodMultiplier=" + to_string(periodMultiplier_));

        splitter_->splitAndSkip(buffer, micInterleaved_, spkInterleaved_, numberOfChannels_.micsUnknown);
        normalize(micInterleaved_, arConfig_.sampleSize);
        normalize(spkInterleaved_, arConfig_.sampleSize);

        const auto& micsDownsampled = micDownsampler(micInterleaved_);
        const auto& spksDownsampled = spkDownsampler(spkInterleaved_);

        convertFPSamplesToInt(channelPerName_[RAW_IL_MIC_STREAM_TYPE], micsDownsampled, 1, RAW_CHANNEL_NORMALIZATION_SCALE);
        convertFPSamplesToInt(channelPerName_[RAW_IL_SPK_STREAM_TYPE], spksDownsampled, 1, RAW_CHANNEL_NORMALIZATION_SCALE);

        addChannelData(micsDownsampled, numberOfChannels_.mics, RAW_MIC_STREAM_TYPE);
        addChannelData(spksDownsampled, numberOfChannels_.spks, RAW_SPK_STREAM_TYPE);

        if (vqeController_->getEngine()) {
            applyVqe(micsDownsampled, spksDownsampled);
        }

        AudioDeviceBase::pushData(channelPerName_);
    }

    void YandexVqeAudioDevice::applyVqe(const std::vector<float>& mics, const std::vector<float>& spks) {
        int micsCaptured = std::count_if(channelPerName_.begin(), channelPerName_.end(), [&](const auto& kv) { return kv.first.find(RAW_MIC_STREAM_TYPE) == 0; });
        int spksCaptured = std::count_if(channelPerName_.begin(), channelPerName_.end(), [&](const auto& kv) { return kv.first.find(RAW_SPK_STREAM_TYPE) == 0; });

        if (micsCaptured != numberOfChannels_.mics || spksCaptured != numberOfChannels_.spks) {
            YIO_LOG_ERROR_EVENT("YandexVqeAudioDevice.InvalidInputData", "[VQE] Invalid amount of input channels in applyVQE mode:"
                                                                             << "  mics captured " << micsCaptured << " when expected " << numberOfChannels_.mics
                                                                             << "; spks captured " << spksCaptured << " when expected " << numberOfChannels_.spks);
            channelPerName_[VQE_CHANNEL_NAME].resize(0);
            return;
        }

        double unusedDoaAngle;
        bool unusedSpeechDetected;

        auto vqeEngine = vqeController_->getEngine();
        vqeEngine->process(mics, spks, unusedDoaAngle, unusedSpeechDetected);
        captureVqeChannels(channelPerName_);

        if (channelPerName_[VQE_CHANNEL_NAME].size() != static_cast<size_t>(arConfig_.periodSize)) {
            YIO_LOG_WARN("[VQE] Unexpected chunk count produced. Actual: " << channelPerName_[VQE_CHANNEL_NAME].size() << " Expected: " << arConfig_.periodSize);
        }
    }

    std::unique_ptr<AudioReader> YandexVqeAudioDevice::createAudioReader(const AudioReaderConfig& config) {
        const int hwChannelCount = config.micChannels + config.spkChannels;

        auto audioReader = std::make_unique<AlsaAudioReader>();
        snd_pcm_format_t pcmFormat;
        switch (config.sampleSize) {
            case 2:
                pcmFormat = SND_PCM_FORMAT_S16_LE;
                break;
            case 4:
                pcmFormat = SND_PCM_FORMAT_S32_LE;
                break;
            default:
                throw std::runtime_error("Unknown sample size");
        }

        const std::string deviceName = config.deviceName.empty() ? AlsaAudioReader::buildHwDeviceName(config.cardNumber, config.deviceNumber) : config.deviceName;
        audioReader->open(deviceName, hwChannelCount, config.inRate, pcmFormat, config.periodSize, 4);

        return audioReader;
    }

    const Json::Value& YandexVqeAudioDevice::getVqeConfig() const {
        return vqeDefaultConfig_;
    }

} // namespace YandexIO
