#pragma once

#include <yandex_io/libs/base/quasar_service.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/modules/audio_input/audio_device/audio_device.h>
#include <yandex_io/modules/audio_input/audio_dumper/audio_dumper.h>
#include <yandex_io/modules/audio_input/broken_mic/broken_mic_detector_system_holder.h>
#include <yandex_io/modules/audio_input/controller/audio_device/simple_controller/simple_audio_device_input_controller.h>
#include <yandex_io/modules/audio_input/controller/mics_state_proxy/audio_input_controller_mics_state_proxy.h>
#include <yandex_io/modules/audio_input/mics_state/hw_provider/hw_mics_state_provider.h>
#include <yandex_io/modules/audio_input/mics_state/metrica_listener/metrica_mics_state_listener.h>
#include <yandex_io/sdk/sdk_interface.h>
#include <yandex_io/sdk/audio_source/i_audio_source.h>

#include <memory>
#include <string>

class AudioSourceService: public QuasarService {
public:
    AudioSourceService(std::string name, std::unique_ptr<YandexIO::AudioDevice> audioDevice, std::shared_ptr<YandexIO::SDKInterface> sdk,
                       std::shared_ptr<YandexIO::IDevice> device, const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory);
    ~AudioSourceService();
    std::string getServiceName() const override;

    void start() override;

private:
    const std::shared_ptr<YandexIO::SDKInterface> sdk_;
    std::shared_ptr<YandexIO::IAudioSource> ioAudioSource_;
    std::shared_ptr<YandexIO::IAudioSourceClient> audioSourceClient_;
    std::shared_ptr<YandexIO::IAudioInputController> audioInputController_;
    std::shared_ptr<YandexIO::AudioDumper> audioDumper_;
    YandexIO::BrokenMicDetectorSystemHolder brokenMicDetector_;
    YandexIO::HwMicsStateProvider fakeMicsStateProvider_;
    std::shared_ptr<YandexIO::AudioInputControllerMicsStateProxy> micsStateProxy_;
    std::shared_ptr<YandexIO::MetricaMicsStateListener> metricaMicsState_;

    const std::string serviceName_;
};
