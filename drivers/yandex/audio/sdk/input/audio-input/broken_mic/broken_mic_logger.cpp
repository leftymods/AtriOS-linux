#include "broken_mic_logger.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/protos/quasar_proto.pb.h>

using namespace quasar;

namespace {

    YandexIO::AudioDevice::ChannelToChunk convert(YandexIO::ChannelsData data) {
        YandexIO::AudioDevice::ChannelToChunk channelsToChunk;
        for (auto& channel : data) {
            YandexIO::AudioDevice::Chunk chunk;
            chunk.data = std::move(channel.data);
            channelsToChunk.insert(std::make_pair(channel.name, std::move(chunk)));
        }
        return channelsToChunk;
    }

} // namespace

namespace YandexIO {

    BrokenMicLogger::BrokenMicLogger(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory, Json::Value audioConfig)
        : defaultConfig_(audioConfig["brokenMicDetector"])
        , toAliced_(ipcFactory->createIpcConnector("aliced"))
    {
        resetToDefault();

        toAliced_->connectToService();
    }

    BrokenMicLogger::~BrokenMicLogger() {
        worker_.destroy();
    }

    void BrokenMicLogger::resetToDefault() {
        YIO_LOG_INFO("Reset BrokenMicDetector to default settings");
        applyConfig(defaultConfig_);
    }

    void BrokenMicLogger::applyConfig(const Json::Value& config) {
        const auto enabled = tryGetBool(config, "enabled", false);
        if (enabled) {
            createBrokenMicDetector(config);
            sendAudioData_ = tryGetBool(config, "sendAudioData", false);
        } else if (detector_) {
            YIO_LOG_INFO("Disable BrokenMicDetector");
            detector_.reset();
        }
        hasDetector_.store(detector_ != nullptr);
    }

    void BrokenMicLogger::updateConfig(const Json::Value& config) {
        worker_.add([this, config]() {
            if (config.isNull()) {
                resetToDefault();
            } else {
                applyConfig(config);
            }
        });
    }

    void BrokenMicLogger::createBrokenMicDetector(const Json::Value& config) {
        const auto detectorConfig = BrokenMicDetector::Config::parseFromJson(config);
        if (detector_ == nullptr || detector_->getConfig() != detectorConfig) {
            YIO_LOG_INFO("Enable broken mic detector with config: " << BrokenMicDetector::Config::toString(detectorConfig));
            detector_ = std::make_unique<BrokenMicDetector>(detectorConfig);
        }
    }

    void BrokenMicLogger::startForcedRun() {
        worker_.add([this]() {
            if (detector_) {
                detector_->startForcedRun();
                YIO_LOG_INFO("Start BrokenMicDetector forced run");
            }
        });
    }

    void BrokenMicLogger::stopForcedRun() {
        worker_.add([this]() {
            if (detector_ && detector_->isForcedRun()) {
                sendBrokenMicNotDetectedEvent();
                detector_->stopForcedRun();
                YIO_LOG_INFO("Stop BrokenMicDetector forced run");
            }
        });
    }

    void BrokenMicLogger::onAudioData(const ChannelsData& data) {
        if (!hasDetector_.load()) {
            return;
        }
        worker_.add([this, data = data]() mutable {
            handleAudioData(std::move(data));
        });
    }

    void BrokenMicLogger::handleAudioData(ChannelsData data) {
        if (!detector_) {
            return;
        }
        /* FIXME: Avoid convert here */
        const auto channelsToChunk = convert(std::move(data));
        checkMics(channelsToChunk);
    }

    void BrokenMicLogger::checkMics(const AudioDevice::ChannelToChunk& chunk) {
        std::vector<float> outRmsRatios;
        AudioDevice::ChannelToChunk outSample;

        const auto status = detector_->catchBrokenMics(chunk, outSample, outRmsRatios);
        bool sendResult = true;
        switch (status) {
            case BrokenMicDetector::Status::NotDetected: {
                sendResult = sendBrokenMicNotDetectedEvent();
                break;
            }
            case BrokenMicDetector::Status::Detected: {
                sendResult = sendBrokenMicDetectedEvent(outRmsRatios, outSample);
                break;
            }
            default:
                return;
        }

        if (!sendResult) {
            detector_->resetNextCheckPoint();
        }
    }

    bool BrokenMicLogger::sendBrokenMicDetectedEvent(const std::vector<float>& rmsRatios, const AudioDevice::ChannelToChunk& samples) {
        YIO_LOG_INFO("Sending broken mic detected event");
        proto::QuasarMessage message;
        auto brokenMicInfo = message.mutable_broken_mic_info();
        if (detector_->isForcedRun()) {
            brokenMicInfo->set_first_setup(true);
            detector_->stopForcedRun();
        }
        brokenMicInfo->set_detected(true);
        for (const auto& value : rmsRatios) {
            brokenMicInfo->add_rms_ratios(value);
        }
        if (sendAudioData_) {
            for (const auto& kv : samples) {
                auto audioInput = brokenMicInfo->add_audio_inputs();
                audioInput->mutable_audio_data()->assign((char*)kv.second.data.data(), kv.second.data.size() * sizeof(int16_t));
                audioInput->set_channel_name(TString(kv.first));
                audioInput->set_doa_angle(0);
            }
        }
        return toAliced_->sendMessage(std::move(message));
    }

    bool BrokenMicLogger::sendBrokenMicNotDetectedEvent() {
        YIO_LOG_INFO("Sending broken mic not detected event");
        proto::QuasarMessage message;
        auto brokenMicInfo = message.mutable_broken_mic_info();
        brokenMicInfo->set_first_setup(detector_->isForcedRun());
        brokenMicInfo->set_detected(false);
        return toAliced_->sendMessage(std::move(message));
    }

} /* namespace YandexIO */
