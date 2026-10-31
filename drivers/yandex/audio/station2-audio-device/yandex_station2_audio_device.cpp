#include "yandex_station2_audio_device.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/modules/audio_input/interleaver/interleaver.h>

#include <yandex_io/modules/audio_input/vqe/controller/vqe_controller.h>

#include <util/system/compiler.h>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

namespace {

    constexpr int BEAMFORMING_DIRECTIONS = 6;
    constexpr auto RAW_MIC_CHANNEL = "raw_mic_";
    constexpr auto RAW_SPK_CHANNEL = "raw_spk_";
    constexpr auto RAW_IL_MIC_CHANNEL_NAME = "raw_il_mic";
    constexpr auto RAW_IL_SPK_CHANNEL_NAME = "raw_il_spk";
    constexpr auto LOOPBACK_CHANNEL = "lb_";
    constexpr auto HDMI_LOOPBACK_CHANNEL = "hdmi_lb_";
    constexpr auto VQE_CHANNEL_NAME = "vqe";

    using YandexStation2ChannelInfo = std::pair<YandexIO::AudioDevice::ChannelsList, YandexIO::YandexVqeAudioDeviceBase::ChannelsCounters>;

    // audio device payload utils
    YandexStation2ChannelInfo prepareAvailableChannels(const Json::Value& config, const std::string& deviceType) {
        const quasar::AudioReaderConfig micReaderConfig(config["micConfig"], deviceType);
        const quasar::AudioReaderConfig spkReaderConfig(config["spkConfig"], deviceType);
        const quasar::AudioReaderConfig lbReaderConfig(config["loopbackConfig"], deviceType);
        const quasar::AudioReaderConfig hdmiReaderConfig(config["hdmiLoopbackConfig"], deviceType);

        const int micChannelsUsed(micReaderConfig.micChannels - 1);
        const int spkChannelsUsed(spkReaderConfig.spkChannels);
        const int lbChannelsUsed(lbReaderConfig.spkChannels);
        const int hdmiChannelsUsed(hdmiReaderConfig.spkChannels);

        YandexIO::AudioDevice::ChannelsList result;
        using ChannelInfo = YandexIO::AudioDevice::ChannelInfo;
        auto addRawChannelNames = [](auto& result, int channelNum, const std::string& channelPrefix, ChannelInfo info) {
            for (int i = 0; i < channelNum; ++i) {
                auto channelName = channelPrefix + std::to_string(i);
                result.emplace(channelName, info);
            }
        };

        addRawChannelNames(result, micChannelsUsed, RAW_MIC_CHANNEL, ChannelInfo{2u, 16000u});
        addRawChannelNames(result, spkChannelsUsed, RAW_SPK_CHANNEL, ChannelInfo{2u, 16000u});
        addRawChannelNames(result, lbChannelsUsed, LOOPBACK_CHANNEL, ChannelInfo{2u, 16000u});
        addRawChannelNames(result, hdmiChannelsUsed, HDMI_LOOPBACK_CHANNEL, ChannelInfo{2u, 16000u});

        result.emplace(RAW_IL_MIC_CHANNEL_NAME, ChannelInfo{2u, 16000u});
        result.emplace(RAW_IL_SPK_CHANNEL_NAME, ChannelInfo{2u, 16000u});

        YandexIO::YandexVqeAudioDeviceBase::ChannelsCounters channelsCounters{
            .mics = micChannelsUsed,
            .spks = spkChannelsUsed + lbChannelsUsed + hdmiChannelsUsed,
            .micsUnknown = 0,
            .hasVqe = true,
        };
        channelsCounters.total = channelsCounters.mics + channelsCounters.spks;
        return {result, channelsCounters};
    }
} // namespace

namespace quasar {
    YandexStation2AudioDevice::YandexStation2AudioDevice(const Json::Value& config,
                                                         const std::string& vqeType,
                                                         const std::string& deviceType,
                                                         bool omniModeSupported,
                                                         std::shared_ptr<YandexIO::ITelemetry> telemetry,
                                                         std::shared_ptr<YandexIO::VqeController> vqeController,
                                                         AudioReaders audioReaders)
        : YandexStation2AudioDevice(config, vqeType, omniModeSupported, std::move(telemetry),
                                    std::move(vqeController),
                                    prepareAvailableChannels(config, deviceType),
                                    std::move(audioReaders))
    {
    }

    YandexStation2AudioDevice::YandexStation2AudioDevice(const Json::Value& config,
                                                         const std::string& vqeType,
                                                         bool omniModeSupported,
                                                         std::shared_ptr<YandexIO::ITelemetry> telemetry,
                                                         std::shared_ptr<YandexIO::VqeController> vqeController,
                                                         YandexStation2ChannelInfo channelInfo,
                                                         AudioReaders audioReaders)
        : YandexIO::YandexVqeAudioDeviceBase(
              channelInfo.second, VQE_CHANNEL_NAME, BEAMFORMING_DIRECTIONS, channelInfo.first,
              std::move(vqeController),
              config)
        , perfLogger_(telemetry)
        , shiftLogger_(telemetry, std::chrono::hours(1), std::chrono::minutes(5))
        , currentVqeType_(vqeType)
        , omniModeSupported_(omniModeSupported)
        , affinityMask_(tryGetInt(config, "affinityMask", 0))
        , vqeQueueSizeLimit_(tryGetInt(config, "vqeQueueSizeLimit", DEFAULT_VQE_QUEUE_LIMIT))
        , audioReaders_(std::move(audioReaders))
        , fullConfig_(config)
    {
        const int allAvaiableFeedbacks = audioReaders_.spkChannelsUsed() + audioReaders_.lbChannelsUsed() + audioReaders_.hdmiChannelsUsed();
        vqePassHdmi_ = (vqeController_->getEngine()->getInputChannelCount().spkCount == static_cast<size_t>(allAvaiableFeedbacks));

        vqeWorker_.add([this]() {
            /* Bind vqe thread to 1 core */
            updateThreadAffinity("vqe");
        });

        captureThread_ = std::thread([this]() {
            alsaReadThread();
        });
    }

    YandexStation2AudioDevice::~YandexStation2AudioDevice() {
        stopped_.store(true);
        if (captureThread_.joinable()) {
            captureThread_.join();
        }
        // reduces AudioDevice reset, which helps vqe
        // can be fixed in QUASAR-9297
        vqeWorker_.clear();
        vqeWorker_.destroy();
    }

    void YandexStation2AudioDevice::onCaptureThreadStart() {
        /* Bind read thread to 1 core */
        updateThreadAffinity("input_read");
        audioReaders_.start();
    }

    void YandexStation2AudioDevice::onCaptureThreadStop() {
        audioReaders_.stop();
    }

    void YandexStation2AudioDevice::alsaReadThread() {
        onCaptureThreadStart();
        /* This thread never stops and read both Mics and Feedback on each iteration
         * When Mics are muted -> this thread continue read both alsa devices and only skip vqe
         * It helps to keep shift between Mic and Feedback constant (approximately sample to sample constant)
         * Constant shift allow vqe to work with the best possible performance
         */
        while (!stopped_.load()) {
            doCapture();
        }
        onCaptureThreadStop();
    }

    void YandexStation2AudioDevice::start() {
        muted_.store(false);
    }

    void YandexStation2AudioDevice::stop() {
        muted_.store(true);
    }

    void YandexStation2AudioDevice::doCapture() {
        auto logSession = perfLogger_.createChunkSession(vqeQueueCounter_.load());
        logSession->start(YandexIO::AudioDeviceStats::Tag::DO_CAPTURE);

        AudioBuffer inBufSpk;
        AudioBuffer inBuffLb;
        AudioBuffer inBufMic;
        AudioBuffer inBufHdmiLb;
        bool errorOnRead = audioReaders_.read(inBufMic, inBufSpk, inBuffLb, inBufHdmiLb, logSession);

        /* skip vqe if muted or error happened on read */
        if (errorOnRead || muted_.load()) {
            return;
        }

        if (vqeQueueCounter_ + 1 <= vqeQueueSizeLimit_) {
            if (vqeQueueOverflowed_) {
                YIO_LOG_INFO("VQE queue restored");
                vqeQueueOverflowed_ = false;
            }

            ++vqeQueueCounter_;
            logSession->start(YandexIO::AudioDeviceStats::Tag::ASYNC_LAG);
            vqeWorker_.add([this, logSession{std::move(logSession)}, inBufMic{std::move(inBufMic)}, inBufSpk{std::move(inBufSpk)}, inBuffLb{std::move(inBuffLb)}, inBufHdmiLb{std::move(inBufHdmiLb)}]() {
                logSession->stop(YandexIO::AudioDeviceStats::Tag::ASYNC_LAG);
                logSession->start(YandexIO::AudioDeviceStats::Tag::PROCESSING);
                processData(inBufMic, inBufSpk, inBuffLb, inBufHdmiLb);
                logSession->stop(YandexIO::AudioDeviceStats::Tag::PROCESSING);
                --vqeQueueCounter_;
            });
        } else if (!vqeQueueOverflowed_) {
            YIO_LOG_ERROR_EVENT("YandexStation2AudioDevice.VqeQueueOverflow", "VQE queue onOverflow");
            vqeQueueOverflowed_ = true;
        }
    }

    /* Change vqe state only from vqe thread */
    void YandexStation2AudioDevice::setSpotterMode() {
        vqeWorker_.add([this]() {
            if (omniModeSupported_) {
                vqeController_->setOmniMode(true);
            }
        });
    }

    void YandexStation2AudioDevice::setASRMode() {
        vqeWorker_.add([this]() {
            if (omniModeSupported_) {
                vqeController_->setOmniMode(false);
            }
        });
    }

    void YandexStation2AudioDevice::processData(const AudioBuffer& inBufMic, const AudioBuffer& inBufSpk,
                                                const AudioBuffer& inBufLb, const AudioBuffer& inHdmiLb) {
        // TODO: when using YandexVQE try not to downsample
        const auto& downsampledSpk = audioReaders_.downsampleSpk(inBufSpk);
        const auto& downsampledLb = audioReaders_.downsampleLb(inBufLb);
        const auto& downsampledMic = audioReaders_.downsampleMic(inBufMic);
        const auto& downsampledHdmi = audioReaders_.downsampleHdmi(inHdmiLb);
        /* Make sure that sizes of Mic and Spk channels are same */
        Y_VERIFY((downsampledMic.size() / audioReaders_.micChannelsUsed()) == (downsampledSpk.size() / audioReaders_.lbChannelsUsed()));

        ChannelToData resultData;
        /* TODO: Avoid intToFloat/floatToInt converts. use int16 data */
        addRawChannels(resultData, downsampledMic, audioReaders_.micChannelsUsed(), RAW_MIC_CHANNEL);
        addRawChannels(resultData, downsampledSpk, audioReaders_.spkChannelsUsed(), RAW_SPK_CHANNEL);
        addRawChannels(resultData, downsampledLb, audioReaders_.lbChannelsUsed(), LOOPBACK_CHANNEL);
        addRawChannels(resultData, downsampledHdmi, audioReaders_.hdmiChannelsUsed(), HDMI_LOOPBACK_CHANNEL);

        /* add raw interleaved channels for smart equalizer */
        convertFPSamplesToInt(resultData[RAW_IL_MIC_CHANNEL_NAME], downsampledMic, 1, AudioReaders::SAMPLE_FLOAT_SCALE);
        convertFPSamplesToInt(resultData[RAW_IL_SPK_CHANNEL_NAME], downsampledSpk, 1, AudioReaders::SAMPLE_FLOAT_SCALE);

        if (vqeController_->getEngine()) {
            bool speechDetected{false};
            double doaAngle{45.0};
            Interleaver interleaver;
            interleaver.add(downsampledSpk, audioReaders_.spkChannelsUsed());
            interleaver.add(downsampledLb, audioReaders_.lbChannelsUsed());
            if (vqePassHdmi_) {
                interleaver.add(downsampledHdmi, audioReaders_.hdmiChannelsUsed());
            }
            interleaver.merge(mergedFeedbacks_);
            vqeController_->getEngine()->process(downsampledMic, mergedFeedbacks_, doaAngle, speechDetected);
            captureVqeChannels(resultData);
            doaAngle_.store(doaAngle); // save doa to atomic variable
            shiftLogger_.log(*vqeController_->getEngine(), currentVqeType_);
        }

        YandexIO::AudioDeviceBase::pushData(resultData);
    }

    void YandexStation2AudioDevice::addRawChannels(ChannelToData& resultData, const std::vector<float>& data,
                                                   size_t channelsNum, const std::string& channelPrefix) {
        if (channelsNum == 0) {
            return;
        }
        std::vector<int16_t> allChannels;
        convertFPSamplesToInt(allChannels, data, 1, AudioReaders::SAMPLE_FLOAT_SCALE);
        Y_VERIFY(allChannels.size() % channelsNum == 0);
        for (size_t j = 0; j < channelsNum; j++) {
            const std::string channelName = channelPrefix + std::to_string(j);
            if (!isChannelCaptured(channelName)) {
                continue;
            }
            std::vector<int16_t>& channelData = resultData[channelName];
            channelData.resize(allChannels.size() / channelsNum);
            for (size_t i = 0, channelPos = 0; i + j < allChannels.size(); i += channelsNum, channelPos++) {
                channelData[channelPos] = allChannels[i + j];
            }
        }
    }

    void YandexStation2AudioDevice::updateThreadAffinity(std::string_view name) {
        if (affinityMask_ == 0) {
            return;
        }
        const pid_t tid = syscall(__NR_gettid);
        const auto syscallres = syscall(__NR_sched_setaffinity, tid, sizeof(affinityMask_), &affinityMask_);
        if (syscallres != 0) {
            YIO_LOG_ERROR_EVENT("YandexStation2AudioDevice.SetAffinityFailed", "Cannot set affinity for " << name << " thread: " << strError(errno));
        } else {
            YIO_LOG_INFO("Updated affinity for " << name << " thread. AffinityMask value: " << affinityMask_);
        }
    }

    double YandexStation2AudioDevice::getDOAAngle() const {
        return doaAngle_.load();
    }

} // namespace quasar
