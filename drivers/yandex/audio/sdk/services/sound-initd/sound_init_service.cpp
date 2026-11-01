#include "sound_init_service.h"

using namespace quasar;

SoundInitService::SoundInitService(std::shared_ptr<YandexIO::IDevice> device,
                                   std::shared_ptr<ipc::IIpcFactory> ipcFactory,
                                   std::shared_ptr<IDeviceStateProvider> deviceStateProvider,
                                   std::shared_ptr<YandexIO::SDKInterface> sdk)
    : device_(std::move(device))
    , ipcFactory_(std::move(ipcFactory))
    , deviceStateProvider_(std::move(deviceStateProvider))
    , sdk_(std::move(sdk))
{
}

std::string SoundInitService::getServiceName() const {
    return SoundInitEndpoint::SERVICE_NAME;
}

std::string SoundInitService::getThreadName() const {
    return "sd_in";
}

void SoundInitService::start() {
    soundInitEndpointPtr_ = std::make_unique<SoundInitEndpoint>(device_, ipcFactory_, deviceStateProvider_,
                                                                YandexIO::createAudioSourceClient(ipcFactory_),
                                                                sdk_->getFilePlayerCapability());
}
