#include "sound_init_endpoint.h"

#include <yandex_io/capabilities/file_player/file_player_capability_proxy.h>

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/setup_parser/sound_utils.h>

#include <yandex_io/modules/audio_input/callback_listener/audio_source_callback_listener.h>

#include <functional>
#include <sstream>

YIO_DEFINE_LOG_MODULE("sound_init");

using namespace quasar;
using namespace SoundUtils;

const std::string SoundInitEndpoint::SERVICE_NAME = "sound_initd";

SoundInitEndpoint::SoundInitEndpoint(
    std::shared_ptr<YandexIO::IDevice> device,
    std::shared_ptr<ipc::IIpcFactory> ipcFactory,
    std::shared_ptr<IDeviceStateProvider> deviceStateProvider,
    std::shared_ptr<YandexIO::IAudioSourceClient> audioSource,
    std::shared_ptr<YandexIO::IFilePlayerCapability> filePlayerCapability)
    : ioAudioSource_(std::move(audioSource))
    , device_(std::move(device))
    , ipcFactory_(std::move(ipcFactory))
    , soundDataReceiver_(device_, SoundUtils::DEFAULT_SAMPLE_RATE)
    , filePlayerCapability_(std::move(filePlayerCapability))
    , deviceContext_(ipcFactory_)
{
    soundDataReceiver_.onDataReceived = std::bind(&SoundInitEndpoint::onDataReceived, this, std::placeholders::_1, std::placeholders::_2);
    soundDataReceiver_.onUnsupportedProtocol = std::bind(&SoundInitEndpoint::onUnsupportedProtocol, this,
                                                         std::placeholders::_1);
    soundDataReceiver_.onTransferStart = std::bind(&SoundInitEndpoint::onTransferStart, this);
    soundDataReceiver_.onTransferError = std::bind(&SoundInitEndpoint::onTransferError, this);

    /* On some devices mic input stream after VQE cannot be used for SoundInit (i.e. on Irbis). For these devices raw
     * input sound should be used. If there is rawInput == true in config -> use raw input
     */
    const auto config = device_->configuration()->getServiceConfig(SERVICE_NAME);
    const auto audioConfig = device_->configuration()->getServiceConfig("audiod");
    const auto rawInput = tryGetBool(config, "rawInput", false);
    if (rawInput) {
        channelType_ = YandexIO::RequestChannelType::RAW;
    }

    YIO_LOG_INFO("sound_initd started. Using [" << (rawInput ? "raw" : "main") << "] channel");

    auto pushData = [this](const YandexIO::ChannelsData& channels) {
        if (channels.empty()) {
            return;
        }

        std::scoped_lock guard(mutex_);
        if (ignoreSound_) {
            return;
        }

        /* TODO: need more smart way to choose channel */
        const auto& firstChannel = channels.front();
        soundDataReceiver_.write(firstChannel.data);
    };

    ioAudioSourceListener_ = std::make_shared<YandexIO::AudioSourceCallbackListener>(std::move(pushData));
    ioAudioSource_->addListener(ioAudioSourceListener_);
    ioAudioSource_->start();

    deviceStateProvider->configurationChangedSignal().connect(
        [this](const auto& deviceState) {
            if (deviceState->configuration == DeviceState::Configuration::CONFIGURING) {
                startInitMode();
            } else {
                stopInitMode();
            }
        }, lifetime_);
}

SoundInitEndpoint::~SoundInitEndpoint()
{
    lifetime_.die();
    stopInitMode();
}

void SoundInitEndpoint::startInitMode()
{
    YIO_LOG_WARN("getting mutex startInitMode");
    std::lock_guard lock(mutex_);
    YIO_LOG_WARN("Locked startInitMode");
    if (initMode_)
    {
        YIO_LOG_WARN("Trying to startInitMode while initMode_==true");
        return;
    }
    if (ioAudioSource_) {
        ioAudioSource_->subscribeToChannels(channelType_);
    }

    setupdConnector_ = ipcFactory_->createIpcConnector("setupd");
    setupdConnector_->setMessageHandler(std::bind(&SoundInitEndpoint::handleQuasarMessage, this, std::placeholders::_1));
    setupdConnector_->connectToService();

    soundDataReceiver_.start();
    initMode_ = true;
    connectionsWakeUpVar_.notify_all();
    YIO_LOG_INFO("Init mode set to " << initMode_);
}

void SoundInitEndpoint::stopInitMode()
{
    {
        std::lock_guard lock(mutex_);
        if (!initMode_)
        {
            YIO_LOG_WARN("Trying to stopInitMode while initMode_==false");
            return;
        }
    }

    soundDataReceiver_.stop();

    std::lock_guard lock(mutex_);
    initMode_ = false;
    if (ioAudioSource_) {
        ioAudioSource_->unsubscribeFromChannels();
    }
    setupdConnector_->shutdown();

    connectionsWakeUpVar_.notify_all();
    YIO_LOG_INFO("Init mode set to " << initMode_);
}

void SoundInitEndpoint::handleQuasarMessage(const ipc::SharedMessage& message)
{
    if (message->has_setup_credentials_handling_completed())
    {
        onSetupCompleted();
    }
}

void SoundInitEndpoint::onSetupCompleted() {
    std::lock_guard lock(mutex_);
    ignoreSound_ = false;
}

void SoundInitEndpoint::fireError(const std::string& wav)
{
    deviceContext_.fireSetupError();
    filePlayerCapability_->playSoundFile(wav, proto::AudioChannel::DIALOG_CHANNEL);
}

void SoundInitEndpoint::onDataReceived(const std::vector<unsigned char>& payload, int protocolVersion)
{
    {
        std::lock_guard lock(mutex_);
        ignoreSound_ = true;
    }

    proto::QuasarMessage message;
    message.mutable_setup_credentials_message()->set_protocol_version(protocolVersion);
    message.mutable_setup_credentials_message()->set_setup_credentials(payload.data(), payload.size());
    message.mutable_setup_credentials_message()->set_source(quasar::proto::SetupSource::SOUND);
    setupdConnector_->sendMessage(std::move(message));
}

void SoundInitEndpoint::onUnsupportedProtocol(int version)
{
    YIO_LOG_ERROR_EVENT("SoundInitEndpoint.UnsupportedProtocolVersion", "We received unsupported sound version " << version);

    fireError("wrong_sound_version.wav");
    Json::Value event;
    event["version"] = std::to_string(version);
    device_->telemetry()->reportEvent("soundInitProtocolVersionUnsupported", jsonToString(event));
}

void SoundInitEndpoint::onTransferStart()
{
    device_->telemetry()->reportEvent("soundInitDataTransferStart");
    soundInitStartLatencyPoint_ = device_->telemetry()->createLatencyPoint();

    deviceContext_.fireSoundDataTransferStart();
}

void SoundInitEndpoint::onTransferError()
{
    fireError("wrong_sound.wav");

    device_->telemetry()->reportError("soundInitTransferError");
    device_->telemetry()->reportLatency(std::move(soundInitStartLatencyPoint_), "soundInitTransferError");

    deviceContext_.fireSetupError();
}

void SoundInitEndpoint::waitUntilInited()
{
    std::unique_lock<std::mutex> lock(mutex_);
    connectionsWakeUpVar_.wait(lock, [this]() {
        return initMode_;
    });
}
