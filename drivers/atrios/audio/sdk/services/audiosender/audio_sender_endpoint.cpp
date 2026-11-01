#include "audio_sender_endpoint.h"

#include "common.h"
#include "log_streamer.h"
#include "platform_info.h"
#include "yandex_io_audio_source_adapter.h"

#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/defines.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/logging/setup/setup.h>
#include <yandex_io/libs/speechkit_logger/speechkit_logger.h>
#include <yandex_io/services/audiosender/info/info.h>

#include <speechkit/Logger.h>
#include <speechkit/SpeechKit.h>

#include <csignal>
#include <fstream>
#include <memory>

YIO_DEFINE_LOG_MODULE("audio_sender");

using namespace quasar;

namespace {

    class LoggingSink: public ::spdlog::sinks::sink {
        void log(const spdlog::details::log_msg& msg) override {
            if (!::spdlog::should_log(msg.level)) {
                return;
            }

            std::stringstream result;
            switch (msg.level) {
                case ::spdlog::level::trace:
                    result << "V ";
                    break;
                case ::spdlog::level::debug:
                    result << "D ";
                    break;
                case ::spdlog::level::info:
                    result << "I ";
                    break;
                case ::spdlog::level::warn:
                    result << "W ";
                    break;
                case ::spdlog::level::err:
                    result << "E ";
                    break;
                default:
                    break;
            }
            result << msg.source.filename << ":" << std::to_string(msg.source.line) << " " << std::string_view{msg.payload.data(), msg.payload.size()};

            AudioSender::LogStreamer::getInstance()->writeLog(result.str());
        }

        void flush() override {
        }
        void set_pattern(const std::string& /*pattern*/) override {
        }
        void set_formatter(std::unique_ptr<spdlog::formatter> /*sink_formatter*/) override {
        }
    };

    void initLogging(std::shared_ptr<YandexIO::IDevice> device) {
        SpeechKit::SpeechKit::getInstance();
        SpeechKit::Logger::setInstance(std::make_shared<YandexIO::SpeechkitLogger>());
        YandexIO::setSpeechkitLogLevel(
            quasar::getString(device->configuration()->getServiceConfig("yiod")["Logging"], "level"));
        quasar::Logging::addLoggingSink(std::make_shared<LoggingSink>());
    }

} // namespace

const std::string AudioSenderEndpoint::SERVICE_NAME = "audiosender";

AudioSenderEndpoint::AudioSenderEndpoint(
    std::shared_ptr<YandexIO::IDevice> device,
    const std::shared_ptr<ipc::IIpcFactory>& ipcFactory,
    SpeechKit::AudioPlayer::SharedPtr audioPlayer,
    std::shared_ptr<quasar::IAuthProvider> authProvider)
    : device_(std::move(device))
    , discoveryAgent_(device_)
    , syncConnector_(ipcFactory->createIpcConnector("syncd"))
    , server_(ipcFactory->createIpcServer("aliced")) // ugly hack to receive legacy media state
    , remotingMessageRouter_(std::make_shared<YandexIO::RemotingMessageRouter>())
{
    const auto& serviceConfig = device_->configuration()->getServiceConfig(SERVICE_NAME);
    const auto runtimeConfigPath = tryGetString(serviceConfig, "runtimeConfigPath");

    Json::Value configJson;
    if (fileExists(runtimeConfigPath)) {
        try {
            configJson = quasar::parseJson(quasar::getFileContent(runtimeConfigPath));
        } catch (const std::exception& err) {
            YIO_LOG_ERROR_EVENT("RuntimeConfigParsingFailed", err.what());
            configJson = serviceConfig;
        }
    } else {
        configJson = serviceConfig;
    }

    initLogging(device_);

    const auto platformInfo = std::make_shared<AudioSender::PlatformInfo>(device_);
    ::SpeechKit::SpeechKit::getInstance()->setPlatformInfo(platformInfo);

    ioAudioSource_ = YandexIO::createAudioSourceClient(ipcFactory);
    auto ioAudioSourceAdapter = std::make_shared<AudioSender::YandexIOAudioSourceAdapter>(device_);
    ioAudioSource_->addListener(ioAudioSourceAdapter);
    audioSourceAdapter_ = std::move(ioAudioSourceAdapter);
    ioAudioSource_->subscribeToChannels(YandexIO::RequestChannelType::ALL);
    ioAudioSource_->start();

    endpointStorage_ = std::make_shared<YandexIO::EndpointStorageHost>(remotingMessageRouter_, device_->deviceId(), NAlice::TEndpoint::SpeakerEndpointType);
    endpointStorage_->init();

    commandServer_ = std::make_shared<AudioSender::CommandServer>(
        configJson, device_, ipcFactory, endpointStorage_, audioSourceAdapter_, std::move(audioPlayer), runtimeConfigPath, std::move(authProvider));

    if (AudioSender::configState(device_) == AudioSender::ConfigState::RUNTIME_ENABLED) {
        syncConnector_->setMessageHandler(std::bind(&AudioSenderEndpoint::onSyncMessage, this, std::placeholders::_1));
        syncConnector_->tryConnectToService();
    }

    server_->setMessageHandler(std::bind(&AudioSenderEndpoint::onMessageToAlice, this, std::placeholders::_1, std::placeholders::_2));

    auto worker = std::make_shared<NamedCallbackQueue>("AudiosenderDeviceStateCapabilityHost");
    deviceStateCapability_ = std::make_shared<YandexIO::DeviceStateCapabilityHost>(worker, remotingMessageRouter_);
    worker->add([this] {
        deviceStateCapability_->init();
        deviceStateCapability_->setAppStateChanged([this](const auto& appState) {
            if (appState.has_music_state()) {
                commandServer_->onMusicState(appState.music_state());
            }
        });
    });

    deviceStateCapability_->addListener(commandServer_);
}

void AudioSenderEndpoint::start() {
    commandServer_->start();
    discoveryAgent_.start();
    server_->listenService();
}

void AudioSenderEndpoint::stop() {
    server_->shutdown();
    discoveryAgent_.stop();
    commandServer_->stop();
}

void AudioSenderEndpoint::onSyncMessage(const ipc::SharedMessage& message) {
    if (!message->has_user_config_update()) {
        return;
    }

    const auto& configUpdate = message->user_config_update();
    if (!configUpdate.has_config()) {
        return;
    }
    const auto& configJson = parseJson(configUpdate.config());

    if (!configJson.isMember("system_config")) {
        return;
    }
    const auto& systemConfigJson = configJson["system_config"];

    const bool audiosenderMode = tryGetBool(systemConfigJson, "audiosenderMode", false);
    if (audiosenderMode) {
        // we are already in audiosender mode
        return;
    }

    YIO_LOG_INFO("Switching to no-audiosender mode.");
    AudioSender::switchLaunchCondition(device_, false);

    stop();

    // suicide in order to be restarted by launcher in new config state
    std::raise(SIGKILL);
}

void AudioSenderEndpoint::onMessageToAlice(
    const ipc::SharedMessage& message, ipc::IServer::IClientConnection& connection)
{
    if (!message->has_remoting()) {
        return;
    }

    remotingMessageRouter_->routeRemotingMessage(message->remoting(), connection.share());
}
