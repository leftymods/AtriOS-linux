#pragma once

#include "gif_animation.h"
#include "led_devices_impl.h"

#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_conductor.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_composition.h>
#include <yandex_io/modules/leds/led_patterns/led_pattern.h>
#include <yandex_io/protos/quasar_proto.pb.h>
#include <yandex_io/modules/bluetooth/bluetooth_observer/bluetooth_observer.h>
#include <yandex_io/libs/bluetooth/bluetooth.h>
#include <yandex_io/modules/bluetooth/bluetooth_controller/bluetooth_controller.h>

#include <atomic>
#include <memory>
#include <mutex>

class YandexStation2BluetoothObserver: public YandexIO::BluetoothObserver {
public:
    YandexStation2BluetoothObserver(std::shared_ptr<YandexIO::BluetoothController> bluetoothController, std::shared_ptr<quasar::LedManager> ledManager, std::shared_ptr<LedDevicesImpl> ledDevices);

    void onDiscoveryStart() override;
    void onDiscoveryStop() override;

    void onSourceConnected(const std::string& btAddr) override;
    void onSourceDisconnected(const std::string& btAddr) override;

    void onChangeVolumeAVRCP(int /* volumePercent */) override{};
    void onFactoryResetComplete() override{};

    void setVisibility(bool connectable, bool discoverable);

    bool isDiscoverable();

private:
    std::shared_ptr<YandexIO::BluetoothController> bluetoothController_;
    std::atomic<bool> isDiscoverable_{false};
    std::shared_ptr<quasar::LedManager> ledManager_;
    std::shared_ptr<LedDevicesImpl> ledDevices_;
};
