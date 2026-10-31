#pragma once

#include "audio_readers.h"

#include <yandex_io/libs/audio/tinyalsa/tiny_alsa_audio_reader.h>
#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/modules/audio_input/audio_device/stats/audio_device_stats.h>
#include <yandex_io/modules/audio_input/audio_device/yandex_vqe/base/yandex_vqe_audio_device_base.h>
#include <yandex_io/modules/audio_input/vqe/feedback_shift_metrics/feedback_shift_logger.h>

#include <atomic>
#include <memory>
#include <string_view>

namespace YandexIO {
    class VqeController;
    enum class FeedbackSource;
} // namespace YandexIO

namespace quasar {

    class YandexStation2AudioDevice: public YandexIO::YandexVqeAudioDeviceBase {
    public:
        explicit YandexStation2AudioDevice(const Json::Value& config,
                                           const std::string& vqeType,
                                           const std::string& deviceType,
                                           bool omniModeSupported,
                                           std::shared_ptr<YandexIO::ITelemetry> telemetry,
                                           std::shared_ptr<YandexIO::VqeController> vqeController,
                                           AudioReaders audioReaders);
        ~YandexStation2AudioDevice();

    public:
        // AudioDevice public api
        void start() override;
        void stop() override;
        void setSpotterMode() override;
        void setASRMode() override;

        double getDOAAngle() const override;

    private:
        YandexStation2AudioDevice(const Json::Value& config,
                                  const std::string& vqeType,
                                  bool omniModeSupported,
                                  std::shared_ptr<YandexIO::ITelemetry> telemetry,
                                  std::shared_ptr<YandexIO::VqeController> vqeController,
                                  std::pair<YandexIO::AudioDevice::ChannelsList, YandexIO::YandexVqeAudioDeviceBase::ChannelsCounters> channelInfo,
                                  AudioReaders audioReaders);

        // AudioDevice private api
        void doCapture() override;
        /**
         * @brief Updated hooks that set capture thread affinity and explicitly start/stop tinyalsa devices
         */
        void onCaptureThreadStart() override;
        void onCaptureThreadStop() override;

        /**
         * @brief apply vqe and push data via YandexIO::AudioDeviceBase::pushData
         */
        void processData(const AudioBuffer& inBufMic, const AudioBuffer& inBufSpk,
                         const AudioBuffer& inBufLb, const AudioBuffer& inHdmiLb);
        void addRawChannels(ChannelToData& resultData, const std::vector<float>& data, size_t channelsNum, const std::string& channelPrefix);
        /* method is not thread safe */
        void setVqeEngine(const std::string& vqeType);

        void alsaReadThread();
        void updateThreadAffinity(std::string_view name);
        void checkAudioReaderConfigs();

        YandexIO::AudioDeviceStats perfLogger_;

        // vqe processing stuff
        std::atomic_bool communicationMode_{false};
        std::atomic_bool muted_{true};
        std::atomic<double> doaAngle_{0.0};
        std::vector<float> mergedFeedbacks_; // reuse same buffer to avoid reallocations
        YandexIO::FeedbackShiftLogger shiftLogger_;

        std::string currentVqeType_;
        bool omniModeSupported_{false};
        bool vqePassHdmi_{false};

        // threading stuff
        std::atomic_bool stopped_{false};
        std::thread captureThread_;
        const int affinityMask_;

        static constexpr int DEFAULT_VQE_QUEUE_LIMIT = 8;
        const int vqeQueueSizeLimit_{DEFAULT_VQE_QUEUE_LIMIT};
        bool vqeQueueOverflowed_{false};
        std::atomic_int vqeQueueCounter_{0};
        quasar::NamedCallbackQueue vqeWorker_{"YandexStation2_vqe"};

        AudioReaders audioReaders_;
        const Json::Value fullConfig_;
    };

} // namespace quasar
