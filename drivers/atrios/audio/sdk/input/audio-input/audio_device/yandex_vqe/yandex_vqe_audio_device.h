#pragma once

#include <yandex_io/libs/audio/reader/audio_reader_config.h>
#include <yandex_io/libs/audio/reader/audio_reader.h>
#include <yandex_io/libs/base/channel_splitter.h>
#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/telemetry/telemetry.h>
#include <yandex_io/modules/audio_input/audio_device/stats/audio_device_stats.h>
#include <yandex_io/modules/audio_input/audio_device/yandex_vqe/base/yandex_vqe_audio_device_base.h>

#include <voicetech/vqe/libs/downsampling/downsampling.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace YandexIO {

    class YandexVqeAudioDevice: public YandexVqeAudioDeviceBase {
    public:
        YandexVqeAudioDevice(const Json::Value& config, const std::string& deviceType,
                             std::shared_ptr<YandexIO::ITelemetry> telemetry);

        ~YandexVqeAudioDevice() override;
        /* from AudioDevice */
        double getDOAAngle() const override;
        void setSpotterMode() override;
        void setASRMode() override;
        /**
         * @brief Create/remove audioReader before/after capture start/stop
         */
        void onCaptureThreadStart() override;
        void onCaptureThreadStop() override;

        const Json::Value& getVqeConfig() const;

    private:
        YandexVqeAudioDevice(const quasar::AudioReaderConfig& arConfig,
                             const std::string& deviceType,
                             const ChannelsCounters& numberOfChannels,
                             const Json::Value& config,
                             std::shared_ptr<YandexIO::ITelemetry> telemetry);
        using ChannelSequence = std::vector<int16_t>;

        void doCapture() override;
        void processData(std::vector<uint8_t>& buffer);
        void addChannelData(const std::vector<float>& buffer, int channelN, const std::string& channelType);
        void applyVqe(const std::vector<float>& mics, const std::vector<float>& spks);
        static std::unique_ptr<quasar::AudioReader> createAudioReader(const quasar::AudioReaderConfig& config);

        const std::shared_ptr<YandexIO::ITelemetry> telemetry_;
        const quasar::AudioReaderConfig arConfig_;
        const ChannelsCounters numberOfChannels_;
        const int frameSize_;
        const int outRate_;
        int periodMultiplier_;
        std::map<std::string, ChannelSequence> channelPerName_;
        std::unique_ptr<quasar::AudioReader> audioReader_;
        quasar::IChannelSplitterPtr splitter_;

        /* yandex vqe mode */
        vqe::BufferedDownsampler<vqe::MultiChannelDownsampler<float, float>> micDownsampler, spkDownsampler;
        std::vector<float> micInterleaved_;
        std::vector<float> spkInterleaved_;

        AudioDeviceStats perfLogger_;
        const int vqeQueueSizeLimit_;
        bool vqeQueueOverflowed_{false};
        std::atomic_int vqeQueueCounter_{0};
        quasar::NamedCallbackQueue worker_{"YandexVqeAudioDevice"};

        Json::Value vqeDefaultConfig_;
    };
} // namespace YandexIO
