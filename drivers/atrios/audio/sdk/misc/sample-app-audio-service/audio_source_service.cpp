#include "audio_source_service.h"

#include <yandex_io/libs/json_utils/json_utils.h>

AudioSourceService::AudioSourceService(std::string name, std::unique_ptr<YandexIO::AudioDevice> audioDevice, std::shared_ptr<YandexIO::SDKInterface> sdk,
                                       std::shared_ptr<YandexIO::IDevice> device, const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory)
    : sdk_(std::move(sdk))
    , brokenMicDetector_(ipcFactory, device->configuration()->getServiceConfig("audiod"))
    , serviceName_(std::move(name))
{
    const auto audiodConfig = device->configuration()->getServiceConfig("audiod");
    ioAudioSource_ = YandexIO::createAudioSource(ipcFactory);
    audioSourceClient_ = ioAudioSource_->createClient();

    /* Configure BrokenMicDetector */
    brokenMicDetector_.init(*sdk_, *audioSourceClient_);

    auto channelTypeConverter = std::bind(YandexIO::defaultConvertChannelNameToType, std::placeholders::_1);
    /* Create audio input controller */
    const auto mainChannel = audiodConfig["mainChannel"].asString();
    audioInputController_ = std::make_shared<YandexIO::SimpleAudioDeviceInputController>(
        std::move(audioDevice), ioAudioSource_, mainChannel, std::move(channelTypeConverter), "",
        std::weak_ptr<YandexIO::VqeController>{});
    micsStateProxy_ = std::make_shared<YandexIO::AudioInputControllerMicsStateProxy>(audioInputController_);
    metricaMicsState_ = std::make_shared<YandexIO::MetricaMicsStateListener>(device->telemetry());

    audioDumper_ = std::make_shared<YandexIO::AudioDumper>(quasar::getString(audiodConfig, "dumpPath"));
    audioSourceClient_->addListener(audioDumper_);
    fakeMicsStateProvider_.addListener(micsStateProxy_);
    fakeMicsStateProvider_.addListener(metricaMicsState_);
}

AudioSourceService::~AudioSourceService() {
    audioDumper_->stopDump();
}

std::string AudioSourceService::getServiceName() const {
    return serviceName_;
}

void AudioSourceService::start() {
    /* Start thread that capture data from audio device and push it to listeners */
    audioSourceClient_->subscribeToChannels(YandexIO::RequestChannelType::ALL);
    audioSourceClient_->start(); // forward audio input data to listeners
    brokenMicDetector_.subscribeToRuntimeConfig(*sdk_);
    /* Force audio dump of all sound to files */
    audioDumper_->startDump();

    fakeMicsStateProvider_.unmute(); /* Notify that device "unmuted", so AudioInoutController will start capture */
}
