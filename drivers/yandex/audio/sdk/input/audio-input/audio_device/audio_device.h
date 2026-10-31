#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace YandexIO {

    class AudioDevice {
    public:
        struct Chunk {
            // data in format (pcm, samplerate=16000, samplesize=2)
            std::vector<int16_t> data;
            double doaAngle;
            Chunk(){};
            Chunk(std::size_t dataSize, double doa)
                : data(dataSize)
                , doaAngle(doa)
                      {};
        };

        struct ChannelInfo {
            unsigned sampleSize; // in bytes, 2 = 16bit
            unsigned sampleRate; // Hz
        };

        using ChannelsList = std::map<std::string, ChannelInfo>;

        // Keys of map are names of channel (i.e. vqe, vqe_spotter, raw_mic_0, raw_mic_7) are platform dependent
        using ChannelToChunk = std::map<std::string, Chunk>;

        AudioDevice(ChannelsList&& availableChannels)
            : availableChannels_{std::move(availableChannels)}
            {};
        virtual ~AudioDevice();

        // Get available channels for capture
        const ChannelsList& getAvailableChannels() {
            return availableChannels_;
        };

        // Starts working thread that reads data from device.
        virtual void start() = 0;
        // Stops working thread that reads data from device.
        virtual void stop() = 0;

        // For some platforms when processing is synchronous (i.e. YandexIO) data in channels will be padded.
        // For linkplay when all is implemented on callbacks padding is not guaranteed.
        virtual ChannelToChunk capture() = 0;

        // fixme: remove methods
        virtual void setSpotterMode() = 0;
        virtual void setASRMode() = 0;

        virtual void startCommunicationMode();
        virtual void stopCommunicationMode();

    private:
        const ChannelsList availableChannels_;
    };

} // namespace YandexIO
