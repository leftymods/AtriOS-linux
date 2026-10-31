#pragma once

#include "audio_config.h"
#include "audio_source_adapter.h"

#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ring_buffer/ring_buffer.h>

#include <json/value.h>

#include <speechkit/SoundBuffer.h>

#include <atomic>
#include <list>
#include <memory>
#include <mutex>

namespace AudioSender {

    struct CommandResult;

    class SoundCollector: public AudioSourceAdapter::Listener, public std::enable_shared_from_this<SoundCollector> {
    public:
        using SharedPtr = std::shared_ptr<SoundCollector>;
        using ChannelSound = std::map<std::string, std::list<SpeechKit::SoundBuffer::SharedPtr>>;

    public:
        SoundCollector(AudioConfig::SharedPtr config,
                       AudioSourceAdapter::SharedPtr quasarAudioSourceAdapter,
                       std::shared_ptr<YandexIO::IDevice> device);

        CommandResult startRecording(uint64_t timestampMs);
        CommandResult stopRecording(uint64_t timestampMs);
        CommandResult getRecordedSound(ChannelSound& sound);
        void setDumpingChannels(const std::unordered_set<std::string>& dumpingChannels);
        const std::unordered_set<std::string>& getDumpingChannels() const;
        uint64_t getRecordingTimeMs() const;

        void disable();
        void enable();

        /* from AudioSourceAdapter::Listener */
        void onData(const AudioSourceAdapter::ChannelToBuffer& chunks) override;

    private:
        struct Chunk final {
            AudioSourceAdapter::ChannelToBuffer channelToChunks;
            uint64_t timestampMs = 0;
        };
        using AudioBuffer = quasar::RingBuffer<Chunk>;
        AudioBuffer::SharedPtr permanentBuffer;
        std::vector<Chunk> resultBuffer;
        std::mutex resultBufferMutex;
        std::atomic_bool isRecording{false};
        std::atomic_bool needToCopyBuffer{false};
        bool subscribed{false};
        uint64_t startTimestampMs;
        uint64_t stopTimestampMs;

        AudioSourceAdapter::SharedPtr quasarAudioSourceAdapter;
        std::unordered_set<std::string> dumpingChannels;

        std::shared_ptr<YandexIO::IDevice> device_;
    };

} // namespace AudioSender
