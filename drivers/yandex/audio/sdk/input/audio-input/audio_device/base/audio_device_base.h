#pragma once

#include <yandex_io/modules/audio_input/audio_device/audio_device.h>

#include <yandex_io/libs/ring_buffer/ring_buffer.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>

#include <json/json.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace YandexIO {

    class AudioDeviceBase: public AudioDevice {
    public:
        using ChannelToBufferMapType = std::map<std::string, quasar::RingBuffer<int16_t>>;

        explicit AudioDeviceBase(ChannelsList&& availableChannels,
                                 const Json::Value& config);
        ~AudioDeviceBase() override;

        void start() override;
        void stop() override;
        ChannelToChunk capture() final;
        void setSpotterMode() override;
        void setASRMode() override;

    protected:
        using ChannelToData = std::map<std::string, std::vector<int16_t>>;
        // Call this method when new data is ready.
        void pushData(const ChannelToData& audioData);
        // Optionally call this method in concrete Audio Device to find out if channel with name channelName is needed.
        // (to avoid unnecessary allocation/processing etc.)
        bool isChannelCaptured(const std::string& channelName) const;

        bool allChannelsAtOnce_ = false;

    private:
        virtual void doCapture() = 0;
        virtual double getDOAAngle() const = 0;

        // simple hooks that can be used by derived class
        virtual void onCaptureThreadStart();
        virtual void onCaptureThreadStop();

        /*
         * If the same instance of audio device may produce different array of channels
         * (e.g. in case of VQE chaning - some of them can produce beamforming in addition)
         * override this method for defining how many channels is absent at the moment
         * By default zero is used
         */
        virtual unsigned getAbsentChannelCount() const;

        void captureThreadProc();

    private:
        bool isAllChannelsCaptured() const;

        std::thread workingThread_;
        std::atomic<bool> isStopped_{true};
        unsigned numberOfChannelsCaptured_ = 0;
        const size_t channelBufferSize_;
        std::mutex dataMutex_;
        quasar::SteadyConditionVariable dataCV_;
        ChannelToBufferMapType channelToBufferMap_;

        bool wasOverflow = false;
    };

} // namespace YandexIO
