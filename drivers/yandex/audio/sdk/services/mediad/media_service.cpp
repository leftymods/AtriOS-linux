#include "media_service.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

using namespace quasar;

MediaService::MediaService(
    std::shared_ptr<YandexIO::IDevice> device,
    std::shared_ptr<ipc::IIpcFactory> ipcFactory,
    std::shared_ptr<quasar::IAuthProvider> authProvider,
    std::shared_ptr<IDeviceStateProvider> deviceStateProvider,
    std::shared_ptr<IMultiroomProvider> multiroomProvider,
    std::shared_ptr<IStereoPairProvider> stereoPairProvider,
    std::shared_ptr<const PlayerFactory> mediaPlayerFactory,
    std::shared_ptr<YandexIO::SDKInterface> sdk)
    : device_(std::move(device))
    , ipcFactory_(std::move(ipcFactory))
    , authProvider_(std::move(authProvider))
    , deviceStateProvider_(std::move(deviceStateProvider))
    , multiroomProvider_(std::move(multiroomProvider))
    , stereoPairProvider_(std::move(stereoPairProvider))
    , mediaPlayerFactory_(std::move(mediaPlayerFactory))
    , sdk_(std::move(sdk))
    , syncdConnector_(ipcFactory_->createIpcConnector("syncd"))
{
}

void MediaService::start()
{
    syncdConnector_->setMessageHandler([this](const ipc::SharedMessage& message) {
        handleSyncMessage(message);
    });
    syncdConnector_->connectToService();

    mediaEndpoint_ = std::make_shared<MediaEndpoint>(device_, ipcFactory_, authProvider_, deviceStateProvider_, mediaPlayerFactory_, sdk_->getDeviceStateCapability(), sdk_->getFilePlayerCapability());

    YIO_LOG_INFO("Starting audio client service");
    audioClientEndpoint_ = std::make_unique<AudioClientEndpoint>(device_, ipcFactory_, mediaPlayerFactory_->audioClockManager(), mediaPlayerFactory_->audioPlayerFactory(), multiroomProvider_, stereoPairProvider_);
}

std::string MediaService::getServiceName() const {
    return MediaEndpoint::SERVICE_NAME;
}

void MediaService::handleSyncMessage(const ipc::SharedMessage& message) {
    if (!message->has_user_config_update()) {
        return;
    }

    const std::string& config = message->user_config_update().config();

    auto jsonConfig = tryParseJson(config);
    if (!jsonConfig) {
        return;
    }

    const auto& mediadConfig = (*jsonConfig)["system_config"][MediaEndpoint::SERVICE_NAME];
    mediaPlayerFactory_->audioPlayerFactory()->configUpdated(mediadConfig);
}
