#pragma once

#include <smart_devices/platforms/yandexstation_2/platformd/audio_device/rcu_audio_device.h>

#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/modules/audio_input/audio_device/base/audio_device_base.h>
#include <yandex_io/modules/audio_input/audio_device/websocket/websocket_audio_config_handler.h>
#include <yandex_io/modules/audio_input/controller/audio_device/utils/utils.h>
#include <yandex_io/modules/audio_input/controller/multiple_vqe_controller/i_multiple_vqe_controller.h>
#include <yandex_io/modules/audio_input/vqe/controller/vqe_controller.h>
#include <yandex_io/sdk/audio_source/feedback_source.h>
#include <yandex_io/sdk/audio_source/i_audio_source.h>
#include <yandex_io/sdk/backend_config_observer.h>

#include <atomic>
#include <memory>
#include <thread>

class AudioInputController final: public YandexIO::IMultipleVqeController, public YandexIO::BackendConfigObserver {
public:
    AudioInputController(
        std::shared_ptr<YandexIO::IDevice> device,
        const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory,
        std::shared_ptr<YandexIO::IAudioSource> audioSource,
        const Json::Value& audioDeviceConfig);

    AudioInputController(
        std::shared_ptr<YandexIO::IDevice> device,
        const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory,
        std::shared_ptr<YandexIO::IAudioSource> audioSource,
        std::shared_ptr<YandexIO::WebsocketAudioConfigHandler> websocketAudioConfigHandler);
    ~AudioInputController();

    void setASRMode() override;
    void setSpotterMode() override;

    /*
     * @brief Mute/Unmute mics (do not affect rcu)
     */
    void mute() override;
    void unmute() override;

    void setActiveVqe(const std::string& activeVqe, const Json::Value& config) override;

    void onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) override;

    /*
     * @brief Switch to rcu audio input using "tag"
     * note Tag is an increasing number (each call tag should be greater than previous one)
     */
    void startRcuCapture(int32_t tag, std::function<void()> onStreamEnd);

    /**
     * @brief Start capture thread
     * @note Should be called once
     */
    void start();

    std::list<std::weak_ptr<YandexIO::VqeController>> getVqeControllers();

    void subscribeToSystemConfig(YandexIO::SDKInterface& sdk);

private:
    void captureMics();
    void scheduleCaptureMics();
    void rcuCaptureThread();

    void setActiveVqeImpl(const Json::Value& config);

    void processConfig(const std::string& jsonConfigValue);
    void changeSyncTarget();
    void resetAudioDevice(const Json::Value& config, bool changeSyncTarget);

    std::optional<int32_t> activeTag_;
    std::function<void()> onStreamEnd_;

    const Json::Value audioDeviceConfig_;
    std::unique_ptr<class IAudioDeviceFactory> audioDeviceFactory_;
    std::shared_ptr<YandexIO::VqeController> vqeController_;
    YandexIO::FeedbackSource hardwareSyncTarget_;
    std::shared_ptr<YandexIO::AudioDeviceBase> micsAudioDevice_;
    std::shared_ptr<const YandexIO::AudioDevice::ChannelsList> micsChannelsList_;
    std::atomic_bool micsAudioDeviceStarted_{false};
    std::atomic_bool micsCommunicationModeStarted_{false};

    std::shared_ptr<YandexIO::IAudioSource> ioAudioSource_;
    std::unique_ptr<RcuAudioDevice> rcuAudioDevice_;
    std::shared_ptr<YandexIO::IDevice> device_;
    std::atomic_bool stopped_{false};
    quasar::NamedCallbackQueue micDeviceConfigCallbackQueue_{"YandexStation2_MicAudioDeviceConfigurer"};
    quasar::NamedCallbackQueue callbackQueue_{"YandexStation2_AudioInputController"};
    quasar::NamedCallbackQueue micsCallbackQueue_{"YandexStation2_MicsCallbackQueue"};

    const YandexIO::ChannelNameToType typeConverter_;
    Json::Value currentAudiodConfig_;

    std::thread rcuWorker_;

    std::string vqeType_;
    std::string vqePreset_;

    bool enableHardwareSyncTarget_{true};
};
