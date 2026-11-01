#pragma once

#include "broken_mic_detector.h"

#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/sdk/audio_source/i_audio_source_client.h>

#include <atomic>
#include <memory>

namespace YandexIO {

    /* Require to be subscribe to AudioSourceClient with all channels requested */
    class BrokenMicLogger: public IAudioSourceClient::Listener {
    public:
        explicit BrokenMicLogger(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory, Json::Value audioConfig);
        ~BrokenMicLogger();

        void onAudioData(const ChannelsData& data) override;

        void updateConfig(const Json::Value& config);

        void startForcedRun();
        void stopForcedRun();

    private:
        void handleAudioData(ChannelsData data);

        void resetToDefault();
        void applyConfig(const Json::Value& config);
        void createBrokenMicDetector(const Json::Value& config);
        void checkMics(const YandexIO::AudioDevice::ChannelToChunk& chunk);
        bool sendBrokenMicDetectedEvent(const std::vector<float>& rmsRatios, const YandexIO::AudioDevice::ChannelToChunk& samples);
        bool sendBrokenMicNotDetectedEvent();

    private:
        const Json::Value defaultConfig_;
        bool sendAudioData_{false};
        std::atomic_bool hasDetector_{false};
        std::unique_ptr<YandexIO::BrokenMicDetector> detector_;
        std::shared_ptr<quasar::ipc::IConnector> toAliced_;
        quasar::NamedCallbackQueue worker_{"BrokenMicLogger"};
    };

} /* namespace YandexIO */
