#include "yandex_station2_bluetooth_observer.h"

#include <yandex_io/libs/logging/logging.h>

#include <util/system/yassert.h>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

using namespace quasar;

void YandexStation2BluetoothObserver::onDiscoveryStart()
{
    isDiscoverable_ = true;
}

void YandexStation2BluetoothObserver::onDiscoveryStop()
{
    isDiscoverable_ = false;
}

bool YandexStation2BluetoothObserver::isDiscoverable()
{
    return isDiscoverable_;
}

void YandexStation2BluetoothObserver::setVisibility(bool connectable, bool discoverable)
{
    bluetoothController_->setVisibility(discoverable, connectable);
    if (discoverable) {
        onDiscoveryStart();
    } else {
        onDiscoveryStop();
    }
}

YandexStation2BluetoothObserver::YandexStation2BluetoothObserver(std::shared_ptr<YandexIO::BluetoothController> bluetoothController, std::shared_ptr<quasar::LedManager> ledManager, std::shared_ptr<LedDevicesImpl> ledDevices)
    : bluetoothController_(std::move(bluetoothController))
    , ledManager_(std::move(ledManager))
    , ledDevices_(std::move(ledDevices))
{
    Y_VERIFY(bluetoothController_ != nullptr);
    Y_VERIFY(ledManager_ != nullptr);
    Y_VERIFY(ledDevices_ != nullptr);
}

void YandexStation2BluetoothObserver::onSourceConnected(const std::string& /*btAddr*/) {
    try {
        auto defaultAnimationComposition = std::make_shared<DefaultAnimationComposition>(
            std::vector<std::shared_ptr<Animation>>{
                GifAnimation::readGif(
                    ledDevices_->getDefaultAnimationsPath() + "bluetooth/25x16_Bluetooth_Connect.gif",
                    ledDevices_->getLedScreen())},
            false);
        auto defaultAnimationConductor = std::make_shared<DefaultAnimationConductor>(defaultAnimationComposition);
        defaultAnimationConductor->setSubstitutionType(SubstitutionType::FOREGROUND);
        ledManager_->play(defaultAnimationConductor);
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("YandexStation2BluetoothObserver.OnSourceConnected.Exception", "failed to draw on led screen: " << e.what());
    }
}

void YandexStation2BluetoothObserver::onSourceDisconnected(const std::string& /*btAddr*/) {
    try {
        ledManager_->stop();
        auto defaultAnimationComposition = std::make_shared<DefaultAnimationComposition>(
            std::vector<std::shared_ptr<Animation>>{
                GifAnimation::readGif(
                    ledDevices_->getDefaultAnimationsPath() + "bluetooth/25x16_Bluetooth_Disconnect.gif",
                    ledDevices_->getLedScreen())},
            false);
        auto defaultAnimationConductor = std::make_shared<DefaultAnimationConductor>(defaultAnimationComposition);
        defaultAnimationConductor->setSubstitutionType(SubstitutionType::FOREGROUND);
        ledManager_->play(defaultAnimationConductor);
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("YandexStation2BluetoothObserver.OnSourceDisconnected.Exception", "failed to draw on led screen: " << e.what());
    }
}
