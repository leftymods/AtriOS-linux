#pragma once

#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/ring_buffer/ring_buffer.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>

#include <map>
#include <memory>
#include <mutex>
#include <vector>

/*
 * Custom "Audio Device" for YandexStation 2 RCU
 */
class RcuAudioDevice final {
public:
    using Tag = int32_t;
    RcuAudioDevice(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory);

    void setActiveTag(Tag tag);

    enum class Status {
        OK,
        FINISHED
    };

    struct Result {
        Tag tag;
        Status status;
    };

    Result capture(std::vector<int16_t>& data);

private:
    void handleQuasarMessage(const quasar::ipc::SharedMessage& msg);
    void saveChunk(Tag tag, const char* data, std::size_t dataSize);

private:
    std::mutex mutex_;
    quasar::SteadyConditionVariable condVar_;
    const size_t channelBufferSize_;
    struct RcuData {
        bool finished{false};
        quasar::RingBuffer<int16_t> buffer;
        RcuData(size_t bufferSize)
            : buffer(bufferSize)
        {
        }
    };
    using TagMap = std::map<Tag, RcuData>;
    TagMap tagToBuffer_;
    TagMap::iterator activeTag_;
    bool hasActiveTag() const;

    std::shared_ptr<quasar::ipc::IConnector> rcuConnector_;
};
