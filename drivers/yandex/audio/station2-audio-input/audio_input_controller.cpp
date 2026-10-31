#include "audio_input_controller.h"

#include <smart_devices/platforms/yandexstation_2/platformd/audio_device/yandex_station2_audio_device.h>
#include <smart_devices/platforms/yandexstation_2/platformd/audio_device/tinyalsa_mixer.h>

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/modules/audio_input/audio_device/tcp/tcp_audio_device.h>
#include <yandex_io/modules/audio_input/audio_device/websocket/websocket_audio_device.h>
#include <yandex_io/modules/audio_input/controller/audio_device/utils/utils.h>
#include <yandex_io/modules/audio_input/vqe/controller/yandex_vqe/vqe_controller_with_yandex_vqe.h>

#include <util/string/cast.h>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

using namespace quasar;

namespace {

    const std::string AUDIOD_CONFIG = "audiod";
    const std::string MICS_MAIN_CHANNEL = "vqe";

    Json::Value getAudioDeviceConfig(
        std::shared_ptr<YandexIO::WebsocketAudioConfigHandler> websocketAudioConfigHandler,
        YandexIO::IDevice* device) {
        auto result = device->configuration()->getServiceConfig("audiod");
        const auto websocketAudioDeviceConfig = websocketAudioConfigHandler->getMarkerConfig();
        quasar::jsonMerge(websocketAudioDeviceConfig, result);
        return result;
    }

    vqe::DigitalFilterConfig getDownsamplerConfig(const Json::Value& config) {
        return vqe::DigitalFilterConfig{
            .designType = vqe::DigitalFilterDesignType::CHEBYSHEV_TYPE_I,
            .order = quasar::tryGetInt(config["downsamplingConfig"], "order", 16),
            .cutoffFrequencyShift = quasar::tryGetDouble(config["downsamplingConfig"], "frequencyShift", 0)};
    }

    bool prepareUseHwFeedbackToMicSync(const Json::Value& config) {
        const bool useHwSync = quasar::tryGetBool(config, "useHwFeedbackToMicSync", false);
        quasar::TinyAlsaMixer mixer;
        if (useHwSync) {
            YIO_LOG_INFO("Use hw sync");
            mixer.enableHardwareSync();
        } else {
            YIO_LOG_INFO("Do not use hw sync");
            mixer.disableHardwareSync();
        }
        return useHwSync;
    }

    AudioReaders prepareAudioReaders(const Json::Value& config, const std::string& deviceType, YandexIO::FeedbackSource hardwareSyncTarget) {
        const bool useHwSync = prepareUseHwFeedbackToMicSync(config);
        const auto downsamplerConfig = getDownsamplerConfig(config);

        IYandexStation2AudioReader::Ptr hdmiLoopback;

        if (quasar::tryGetBool(config, "readHdmiLoopback", false)) {
            YIO_LOG_INFO("Send hdmi loopback");
            hdmiLoopback = YandexStation2AudioReader::createSpk(config["hdmiLoopbackConfig"], deviceType, downsamplerConfig, "HdmiLoopbackAudioReader");
        } else {
            YIO_LOG_INFO("Send fake hdmi loopback");
            hdmiLoopback = std::make_unique<ZerosAudioReader>(2, 2048);
        }

        /**
         * Note: DO NOT CHANGE TinyAlsaAudioReaders Order
         * It should be guaranteed that Spk Alsa device will be used before Mics alsa device:
         * So open spk first -> then mics; Read spk first -> then mics;
         */
        return AudioReaders(
            {YandexStation2AudioReader::createSpk(config["spkConfig"], deviceType, downsamplerConfig, "SpkAudioReader"),
             YandexStation2AudioReader::createMic(config["micConfig"], deviceType, downsamplerConfig, "MicAudioReader", useHwSync),
             YandexStation2AudioReader::createSpk(config["loopbackConfig"], deviceType, downsamplerConfig, "SwLoopbackAudioReader"),
             std::move(hdmiLoopback)},
            useHwSync,
            hardwareSyncTarget);
    }

    const char* feedbackSourceToString(YandexIO::FeedbackSource source) {
        switch (source) {
            case YandexIO::FeedbackSource::HW:
                return "HW";
            case YandexIO::FeedbackSource::SW:
                return "SW";
            case YandexIO::FeedbackSource::HDMI:
                return "HDMI";
            default:
                YIO_LOG_ERROR_EVENT("AudioInputController.UnknownLoopbackSource", "Feedback source is unknown");
                throw std::runtime_error("Feedback source is unknown");
        }
    }
} // namespace

class IAudioDeviceFactory {
public:
    using AudioDeviceFactoryPtr = std::unique_ptr<IAudioDeviceFactory>;
    using VqeControllerPtr = std::shared_ptr<YandexIO::VqeController>;
    using AudioDevicePtr = std::unique_ptr<YandexIO::AudioDeviceBase>;

    IAudioDeviceFactory(
        const std::string& deviceType,
        std::shared_ptr<YandexIO::ITelemetry> telemetry)
        : deviceType_(deviceType)
        , telemetry_(telemetry)
    {
    }

    IAudioDeviceFactory(const IAudioDeviceFactory&) = delete;
    IAudioDeviceFactory(IAudioDeviceFactory&&) = delete;

    IAudioDeviceFactory& operator=(const IAudioDeviceFactory&) = delete;
    IAudioDeviceFactory& operator=(IAudioDeviceFactory&&) = delete;

    virtual ~IAudioDeviceFactory() = default;

    virtual VqeControllerPtr openVqe(const Json::Value& /* config */) = 0;

    virtual AudioDevicePtr openAudioDevice(
        const Json::Value& config,
        VqeControllerPtr vqe,
        YandexIO::FeedbackSource syncSource) = 0;

protected:
    const std::string deviceType_;
    std::shared_ptr<YandexIO::ITelemetry> telemetry_;
    const Json::Value config_;
};

class YandexStation2AudioDeviceFactory: public IAudioDeviceFactory {
public:
    using IAudioDeviceFactory::IAudioDeviceFactory;

    VqeControllerPtr openVqe(const Json::Value& audioDeviceConfig) override {
        auto vqeController = YandexIO::makeVqeControllerWithYandexVqe(telemetry_, deviceType_);
        const auto vqeType = tryGetString(audioDeviceConfig, "defaultVqeType", "yandex");
        const auto& vqeInfo = audioDeviceConfig[vqeType + "_vqe"];
        const std::string name = vqeInfo["name"].asString();
        YIO_LOG_DEBUG("Creating VQE engine " << vqeType << " with config " << jsonToString(vqeInfo));
        vqeController->setEngine(vqeInfo);
        return vqeController;
    }

    AudioDevicePtr openAudioDevice(
        const Json::Value& audioDeviceConfig,
        VqeControllerPtr vqe,
        YandexIO::FeedbackSource syncSource) override {
        const auto vqeType = tryGetString(audioDeviceConfig, "defaultVqeType", "yandex");
        const auto& vqeInfo = audioDeviceConfig[vqeType + "_vqe"];
        return std::make_unique<YandexStation2AudioDevice>(
            audioDeviceConfig,
            deviceType_,
            vqeType,
            tryGetBool(vqeInfo, "omniMode", false),
            telemetry_,
            std::move(vqe),
            prepareAudioReaders(audioDeviceConfig, deviceType_, syncSource));
    }
};

class TCPAudioDeviceFactory: public IAudioDeviceFactory {
    using IAudioDeviceFactory::IAudioDeviceFactory;

    VqeControllerPtr openVqe(const Json::Value& /* config */) override {
        return {};
    }

    AudioDevicePtr openAudioDevice(
        const Json::Value& config,
        VqeControllerPtr /* vqe */,
        YandexIO::FeedbackSource /* syncSource */) override {
        return std::make_unique<YandexIO::TCPAudioDevice>(config);
    }
};

class WebsocketAudioDeviceFactory: public IAudioDeviceFactory {
    using IAudioDeviceFactory::IAudioDeviceFactory;

    VqeControllerPtr openVqe(const Json::Value& /* config */) override {
        return {};
    }

    AudioDevicePtr openAudioDevice(
        const Json::Value& config,
        VqeControllerPtr /* vqe */,
        YandexIO::FeedbackSource /* syncSource */) override {
        return std::make_unique<YandexIO::WebsocketAudioDevice>(config, telemetry_);
    }
};

IAudioDeviceFactory::AudioDeviceFactoryPtr openAudioDeviceFactory(
    const Json::Value& config,
    std::shared_ptr<YandexIO::ITelemetry> telemetry,
    const std::string& deviceType) {
    const std::string deviceName = tryGetString(config["audioDevice"], "device", "default");
    if (deviceName == "default") {
        return std::make_unique<YandexStation2AudioDeviceFactory>(deviceType, telemetry);
    } else if (deviceName == "tcp") {
        return std::make_unique<TCPAudioDeviceFactory>(deviceType, telemetry);
    } else if (deviceName == "websocket") {
        return std::make_unique<WebsocketAudioDeviceFactory>(deviceType, telemetry);
    } else {
        throw std::logic_error("Unknown audio device type");
    }
}

AudioInputController::AudioInputController(
    std::shared_ptr<YandexIO::IDevice> device,
    const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory,
    std::shared_ptr<YandexIO::IAudioSource> audioSource,
    std::shared_ptr<YandexIO::WebsocketAudioConfigHandler> websocketAudioConfigHandler)
    : AudioInputController(device, ipcFactory, audioSource, getAudioDeviceConfig(websocketAudioConfigHandler, device.get()))
{
}

AudioInputController::AudioInputController(
    std::shared_ptr<YandexIO::IDevice> device,
    const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory,
    std::shared_ptr<YandexIO::IAudioSource> audioSource,
    const Json::Value& audioDeviceConfig)
    : audioDeviceConfig_(audioDeviceConfig)
    , audioDeviceFactory_(openAudioDeviceFactory(audioDeviceConfig_, device->telemetry(), device->configuration()->getDeviceType()))
    , vqeController_(audioDeviceFactory_->openVqe(audioDeviceConfig_))
    , hardwareSyncTarget_(YandexIO::FeedbackSource::HW)
    , micsAudioDevice_(audioDeviceFactory_->openAudioDevice(audioDeviceConfig_, vqeController_, hardwareSyncTarget_))
    , micsChannelsList_(std::make_shared<const YandexIO::AudioDevice::ChannelsList>(micsAudioDevice_->getAvailableChannels()))
    , ioAudioSource_(std::move(audioSource))
    , rcuAudioDevice_(std::make_unique<RcuAudioDevice>(ipcFactory))
    , device_(std::move(device))
    , typeConverter_(std::bind(YandexIO::defaultConvertChannelNameToType, std::placeholders::_1))
    , currentAudiodConfig_(Json::Value::null)
    , enableHardwareSyncTarget_(true)
{
    if (auto vqeController = vqeController_) {
        std::tie(vqeType_, vqePreset_) = vqeController->getVqeTypeAndPreset();
    }
}

AudioInputController::~AudioInputController() {
    stopped_.store(true);
    if (rcuWorker_.joinable()) {
        rcuWorker_.join();
    }
    callbackQueue_.destroy();
    micsCallbackQueue_.destroy();
}

void AudioInputController::start() {
    rcuWorker_ = std::thread([this]() {
        rcuCaptureThread();
    });
    scheduleCaptureMics();
}

void AudioInputController::rcuCaptureThread() {
    while (!stopped_.load()) {
        YandexIO::ChannelData channel;
        channel.name = "raw_rcu";
        channel.type = YandexIO::ChannelData::Type::RAW;
        channel.isForRecognition = true;
        const auto result = rcuAudioDevice_->capture(channel.data);

        callbackQueue_.add([this, channel{std::move(channel)}, result]() mutable {
            if (!activeTag_.has_value()) {
                // do not have active tag. skip buffer
                return;
            }
            if (activeTag_.value() != result.tag) {
                // skip expired tag
                return;
            }
            if (result.status == RcuAudioDevice::Status::FINISHED) {
                YIO_LOG_INFO("Finish rcu capture with tag: " << activeTag_.value());
                if (onStreamEnd_) {
                    onStreamEnd_();
                }
                activeTag_.reset();
                onStreamEnd_ = nullptr;
                // stream finished. Wait next tag
                return;
            }

            ioAudioSource_->pushData({std::move(channel)});
        });
    }
}

void AudioInputController::captureMics() {
    // This function hangs if micsAudioDevice stopped
    // Make sure it's in micsCallbackQueue - synced with mute/unmute
    // Make sure it's called under micsAudioDeviceStarted_ == true
    Y_ENSURE_THREAD(&micsCallbackQueue_);
    auto chunks = micsAudioDevice_->capture();
    callbackQueue_.add([this, chunks{std::move(chunks)}, micsChannelList{micsChannelsList_}]() mutable {
        if (activeTag_.has_value()) {
            return;
        }
        ioAudioSource_->pushData(convertAudioDeviceChannels(
            std::move(chunks), MICS_MAIN_CHANNEL, typeConverter_, *micsChannelList, vqeType_, vqePreset_));
    });
    if (auto vqeController = vqeController_) {
        const auto hardwareSyncTarget = vqeController->getEngine()->hardwareSyncTarget();
        if (enableHardwareSyncTarget_ && hardwareSyncTarget_ != hardwareSyncTarget) {
            YIO_LOG_INFO("vqeController changed hardwareSyncTarget from " << ToString(hardwareSyncTarget_) << " to " << ToString(hardwareSyncTarget));
            hardwareSyncTarget_ = hardwareSyncTarget;
            micsCallbackQueue_.clear();
            micsCallbackQueue_.add([this]() {
                changeSyncTarget();
            });
        }
    }
}

void AudioInputController::scheduleCaptureMics() {
    micsCallbackQueue_.add([this]() {
        if (micsAudioDeviceStarted_.load()) {
            captureMics();
            scheduleCaptureMics();
        }
    });
}

// XXX: check thread safety for setASRMode and setSpotterMode
void AudioInputController::setASRMode() {
    micsCallbackQueue_.add([this]() {
        YIO_LOG_DEBUG("Set input audio device to ASR mode");
        micsAudioDevice_->setASRMode();
    });
}

void AudioInputController::setSpotterMode() {
    micsCallbackQueue_.add([this]() {
        YIO_LOG_DEBUG("Set input audio device to Spotter mode");
        micsAudioDevice_->setSpotterMode();
    });
}

void AudioInputController::mute() {
    micsCallbackQueue_.add([this]() {
        YIO_LOG_INFO("Mute microphones");
        micsAudioDeviceStarted_.store(false);
        micsAudioDevice_->stop();
    });
}

void AudioInputController::unmute() {
    micsCallbackQueue_.add([this]() {
        YIO_LOG_INFO("Unmute microphones");
        micsAudioDeviceStarted_.store(true);
        micsAudioDevice_->start();
        scheduleCaptureMics();
    });
}

void AudioInputController::startRcuCapture(int32_t tag, std::function<void()> onStreamEnd) {
    YIO_LOG_INFO("Start rcu capture with tag: " << tag);
    callbackQueue_.add([this, tag, onStreamEnd{std::move(onStreamEnd)}]() mutable {
        activeTag_ = tag;
        onStreamEnd_ = std::move(onStreamEnd);
        rcuAudioDevice_->setActiveTag(tag);
    });
}

void AudioInputController::setActiveVqe(const std::string& activeVqe, const Json::Value& config) {
    if (activeVqe != "yandex") {
        YIO_LOG_WARN("Station-2 doesn't support vqe switch, only yandex vqe is supported")
        return;
    }

    if (const std::string cfgName = activeVqe + "_vqe"; audioDeviceConfig_.isMember(cfgName)) {
        Json::Value newConfig = audioDeviceConfig_[cfgName];
        if (newConfig.isObject()) {
            YandexIO::mergeVqeConfigsWithJsonPreset(config, newConfig);
            setActiveVqeImpl(newConfig);
            return;
        }
    }
    setActiveVqeImpl(config);
};

void AudioInputController::onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) {
    micsCallbackQueue_.add([this, configName, jsonConfigValue]() {
        if (configName == AUDIOD_CONFIG) {
            processConfig(jsonConfigValue);
        }
    });
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void AudioInputController::subscribeToSystemConfig(YandexIO::SDKInterface& sdk) {
    sdk.subscribeToSystemConfig(AUDIOD_CONFIG);
}

void AudioInputController::setActiveVqeImpl(const Json::Value& config) {
    YIO_LOG_INFO("Trying new VQE config: " << config);
    if (auto vqeController = vqeController_) {
        YIO_LOG_INFO("Changed vqe to config: " << config);
        vqeController->setEngine(config);
        std::tie(vqeType_, vqePreset_) = vqeController->getVqeTypeAndPreset();
    }
}

void AudioInputController::processConfig(const std::string& jsonConfigValue) {
    Y_ENSURE_THREAD(&micsCallbackQueue_);
    auto result = tryParseJsonOrEmpty(jsonConfigValue);
    const bool enableHardwareSyncTarget = tryGetBool(result, "enableHardwareSyncTarget", true);
    bool changeSyncTarget = enableHardwareSyncTarget;
    if (enableHardwareSyncTarget_ && !enableHardwareSyncTarget) { // set sync target to HW, when disable changes
        hardwareSyncTarget_ = YandexIO::FeedbackSource::HW;
        changeSyncTarget = true;
    }
    enableHardwareSyncTarget_ = enableHardwareSyncTarget;
    if (result != currentAudiodConfig_) {
        YIO_LOG_INFO("Reset audio device on config upadte: " << result);
        YIO_LOG_INFO("enableHardwareSyncTarget: " << enableHardwareSyncTarget_)
        currentAudiodConfig_ = result;
        quasar::jsonMerge(audioDeviceConfig_, result);
        resetAudioDevice(result, changeSyncTarget);
    }
}

void AudioInputController::changeSyncTarget() {
    Y_ENSURE_THREAD(&micsCallbackQueue_);
    if (!enableHardwareSyncTarget_) {
        return;
    }
    auto result = currentAudiodConfig_;
    quasar::jsonMerge(audioDeviceConfig_, result);
    resetAudioDevice(result, true);
}

void AudioInputController::resetAudioDevice(const Json::Value& config, bool changeSyncTarget) {
    micsAudioDevice_.reset();
    YandexIO::FeedbackSource hardwareSyncTarget = YandexIO::FeedbackSource::HW;
    if (changeSyncTarget) {
        TinyAlsaMixer mixer;
        hardwareSyncTarget = hardwareSyncTarget_;
        device_->telemetry()->reportEvent("audioHardwareSyncChanged", feedbackSourceToString(hardwareSyncTarget));
        mixer.setLoopbackSource(hardwareSyncTarget_);
    }
    micsAudioDevice_ = audioDeviceFactory_->openAudioDevice(
        config,
        vqeController_,
        hardwareSyncTarget);
    micsChannelsList_ = std::make_shared<const YandexIO::AudioDevice::ChannelsList>(micsAudioDevice_->getAvailableChannels());
    if (micsAudioDeviceStarted_.load()) {
        micsAudioDevice_->start();
    }
    scheduleCaptureMics();
}

std::list<std::weak_ptr<YandexIO::VqeController>> AudioInputController::getVqeControllers() {
    return {vqeController_};
}
