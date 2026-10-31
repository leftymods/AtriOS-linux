#pragma once

#include <yandex_io/interfaces/user_config/i_user_config_provider.h>
#include <yandex_io/libs/audio_player/base/audio_player.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/modules/audio_input/callback_listener/audio_source_callback_listener.h>
#include <yandex_io/sdk/audio_source/i_audio_source_client.h>

#include <contrib/libs/webrtc/api/audio_options.h>
#include <contrib/libs/webrtc/modules/audio_device/include/audio_device.h>
#include <contrib/libs/webrtc/modules/audio_processing/include/audio_processing.h>

#include <json/json.h>

#include <mutex>

namespace quasar {

    class QuasarAudioDeviceModule: public webrtc::AudioDeviceModule {
    public:
        QuasarAudioDeviceModule(
            const std::shared_ptr<ipc::IIpcFactory>& ipcFactory,
            std::shared_ptr<IUserConfigProvider> userConfigProvider,
            Json::Value callsConfig);

        ~QuasarAudioDeviceModule();

    public:
        static ::rtc::scoped_refptr<QuasarAudioDeviceModule> create(
            const std::shared_ptr<ipc::IIpcFactory>& ipcFactory,
            std::shared_ptr<IUserConfigProvider> userConfigProvider,
            Json::Value callsConfig);

    public:
        int32_t ActiveAudioLayer(AudioLayer* audio_layer) const override;

        int32_t RegisterAudioCallback(webrtc::AudioTransport* audioCallback) override;

        int32_t Init() override;
        int32_t Terminate() override;
        bool Initialized() const override;

        int16_t PlayoutDevices() override;
        int16_t RecordingDevices() override;
        int32_t PlayoutDeviceName(
            uint16_t index,
            char name[webrtc::kAdmMaxDeviceNameSize],
            char guid[webrtc::kAdmMaxGuidSize]) override;
        int32_t RecordingDeviceName(
            uint16_t index,
            char name[webrtc::kAdmMaxDeviceNameSize],
            char guid[webrtc::kAdmMaxGuidSize]) override;

        int32_t SetPlayoutDevice(uint16_t index) override;
        int32_t SetPlayoutDevice(WindowsDeviceType device) override;
        int32_t SetRecordingDevice(uint16_t index) override;
        int32_t SetRecordingDevice(WindowsDeviceType device) override;

        int32_t PlayoutIsAvailable(bool* available) override;
        int32_t InitPlayout() override;
        bool PlayoutIsInitialized() const override;
        int32_t RecordingIsAvailable(bool* available) override;
        int32_t InitRecording() override;
        bool RecordingIsInitialized() const override;

        int32_t StartPlayout() override;
        int32_t StopPlayout() override;
        bool Playing() const override;
        int32_t StartRecording() override;
        int32_t StopRecording() override;
        bool Recording() const override;

        int32_t InitSpeaker() override;
        bool SpeakerIsInitialized() const override;
        int32_t InitMicrophone() override;
        bool MicrophoneIsInitialized() const override;

        int32_t SpeakerVolumeIsAvailable(bool* available) override;
        int32_t SetSpeakerVolume(uint32_t volume) override;
        int32_t SpeakerVolume(uint32_t* volume) const override;
        int32_t MaxSpeakerVolume(uint32_t* max_volume) const override;
        int32_t MinSpeakerVolume(uint32_t* min_volume) const override;

        int32_t MicrophoneVolumeIsAvailable(bool* available) override;
        int32_t SetMicrophoneVolume(uint32_t volume) override;
        int32_t MicrophoneVolume(uint32_t* volume) const override;
        int32_t MaxMicrophoneVolume(uint32_t* max_volume) const override;
        int32_t MinMicrophoneVolume(uint32_t* min_volume) const override;

        int32_t SpeakerMuteIsAvailable(bool* available) override;
        int32_t SetSpeakerMute(bool enable) override;
        int32_t SpeakerMute(bool* enabled) const override;

        int32_t MicrophoneMuteIsAvailable(bool* available) override;
        int32_t SetMicrophoneMute(bool enable) override;
        int32_t MicrophoneMute(bool* enabled) const override;

        int32_t StereoPlayoutIsAvailable(bool* available) const override;
        int32_t SetStereoPlayout(bool enable) override;
        int32_t StereoPlayout(bool* enabled) const override;
        int32_t StereoRecordingIsAvailable(bool* available) const override;
        int32_t SetStereoRecording(bool enable) override;
        int32_t StereoRecording(bool* enabled) const override;

        int32_t PlayoutDelay(uint16_t* delay_ms) const override;

        bool BuiltInAECIsAvailable() const override;
        bool BuiltInAGCIsAvailable() const override;
        bool BuiltInNSIsAvailable() const override;

        int32_t EnableBuiltInAEC(bool enable) override;
        int32_t EnableBuiltInAGC(bool enable) override;
        int32_t EnableBuiltInNS(bool enable) override;

    public:
        webrtc::AudioTransport* audioTransport();

    private:
        void actualizePlayer(
            const Json::Value& calldConfig,
            const Json::Value& sinkOptions = Json::objectValue);
        void initializePlayer(
            const Json::Value& calldConfig);

    private:
        int32_t StopPlayoutNoLock();
        int32_t StopRecordingNoLock();

    public:
        cricket::AudioOptions getAudioOptions() const;

        webrtc::AudioProcessing::Config getAudioProcessingConfig() const;

    private:
        void handleAudioInput(const YandexIO::ChannelData& channelData);

    private:
        struct Channel {
            static const int DEFAULT_CHANNELS = 1;
            static const int DEFAULT_SAMPLE_RATE = 16000;
            static const int DEFAULT_SAMPLE_SIZE = 2;

            std::string name;
            YandexIO::RequestChannelType type;
            int channels;
            int sampleRate;
            int sampleSize;

            int getSamplesIn10Ms() const {
                return sampleRate / 100;
            }

            int getBytesIn10Ms() const {
                return getSamplesIn10Ms() * sampleSize;
            }
        };

        void initMainChannelAndAudioOptions(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory);

    private:
        mutable std::mutex lock_;

        std::unique_ptr<quasar::AudioPlayer> player_;

        Channel mainChannel_;
        cricket::AudioOptions audioOptions_;
        webrtc::AudioProcessing::Config audioProcessingConfig_;

        webrtc::AudioTransport* audioCallback_ = nullptr;

        bool initialized_ = false;
        bool playoutInitialized_ = false;
        bool recordingInitialized_ = false;

        std::atomic_bool recording_{false};
        std::atomic_bool playing_{false};

        std::vector<char> audioBuffer_;
        int audioBufferEnd_ = 0;

        const Json::Value defaultConfig_;
        std::shared_ptr<IUserConfigProvider> userConfigProvider_;
        Lifetime preparePlayerLifetime_;

        /* New audio Source */
    private:
        std::shared_ptr<YandexIO::AudioSourceCallbackListener> ioAudioSourceListener_;
        std::shared_ptr<YandexIO::IAudioSourceClient> ioAudioSource_;
    };

} // namespace quasar
