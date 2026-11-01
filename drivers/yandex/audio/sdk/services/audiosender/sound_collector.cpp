#include "sound_collector.h"

#include "command_result.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/defines.h>

using namespace quasar;
using namespace AudioSender;

SoundCollector::SoundCollector(AudioConfig::SharedPtr config,
                               AudioSourceAdapter::SharedPtr quasarAudioSourceAdapter,
                               std::shared_ptr<YandexIO::IDevice> device)
    : quasarAudioSourceAdapter(quasarAudioSourceAdapter)
    , device_(device)
{
    // One read operation returns periodSize frames
    // sampleRate / periodSize = number of read operations in 1 second
    permanentBuffer = std::make_shared<AudioBuffer>(
        config->getHistoryDurationSec() * 1000 / quasarAudioSourceAdapter->getBufferCaptureTimeout());
}

CommandResult SoundCollector::startRecording(uint64_t timestampMs) {
    uint64_t nowTimestampMs = getNowTimestampMs();

    if (timestampMs > nowTimestampMs) {
        return {false, "future timestamp is prohibited"};
    }

    // start load chunks to resultBuffer
    needToCopyBuffer.store(true);
    isRecording.store(true);

    startTimestampMs = (timestampMs == 0 ? nowTimestampMs : timestampMs);

    return true;
}

CommandResult SoundCollector::stopRecording(uint64_t timestampMs) {
    if (!isRecording.load()) {
        return {true, ""};
    }

    uint64_t nowTimestampMs = getNowTimestampMs();

    if (timestampMs != 0) {
        if (timestampMs > nowTimestampMs) {
            return {false, "future timestamp is prohibited"};
        }

        if (timestampMs <= startTimestampMs) {
            return {false, "Too old timestamp"};
        }
    }

    isRecording.store(false);

    stopTimestampMs = (timestampMs == 0 ? nowTimestampMs : timestampMs);

    return true;
}

CommandResult SoundCollector::getRecordedSound(ChannelSound& sound) {
    if (isRecording.load()) {
        return {false, "Stop recording before getting sound!"};
    }

    std::lock_guard<std::mutex> lockResult(resultBufferMutex);
    if (resultBuffer.empty()) {
        return {false, "Empty recorded sound."};
    }

    // find and erase excess chunks
    auto begin = std::find_if(resultBuffer.begin(), resultBuffer.end(), [this](const Chunk& chunk) {
        return chunk.timestampMs > startTimestampMs;
    });
    auto end = std::find_if(resultBuffer.rbegin(), resultBuffer.rend(), [this](const Chunk& chunk) {
                   return chunk.timestampMs < stopTimestampMs;
               }).base();
    if (end != resultBuffer.end()) {
        resultBuffer.erase(++end, resultBuffer.end());
    }
    resultBuffer.erase(resultBuffer.begin(), begin);

    for (const auto& chunk : resultBuffer) {
        for (const auto& channelToChunk : chunk.channelToChunks) {
            sound[channelToChunk.first].push_back(channelToChunk.second);
        }
    }

    return true;
}

void SoundCollector::setDumpingChannels(const std::unordered_set<std::string>& dumpingChannels) {
    if (this->dumpingChannels != dumpingChannels) {
        this->dumpingChannels = dumpingChannels;
        if (subscribed) {
            disable();
            enable();
        }
    }
}

const std::unordered_set<std::string>& SoundCollector::getDumpingChannels() const {
    return dumpingChannels;
}

uint64_t SoundCollector::getRecordingTimeMs() const {
    return stopTimestampMs - startTimestampMs;
}

void SoundCollector::disable() {
    if (!subscribed) {
        return;
    }

    subscribed = false;
    quasarAudioSourceAdapter->unsubscribe(shared_from_this());
}

void SoundCollector::enable() {
    if (subscribed) {
        return;
    }

    permanentBuffer->clear();
    subscribed = true;
    quasarAudioSourceAdapter->subscribe(shared_from_this(), dumpingChannels);
}

void SoundCollector::onData(const AudioSourceAdapter::ChannelToBuffer& chunks) {
    Chunk chunk;
    chunk.channelToChunks = chunks;
    chunk.timestampMs = getNowTimestampMs();

    if (isRecording.load()) {
        // write chunk to resultBuffer
        std::lock_guard<std::mutex> lockResult(resultBufferMutex);

        bool checkNeedToCopyBuffer = true;
        if (needToCopyBuffer.compare_exchange_strong(checkNeedToCopyBuffer, false)) {
            device_->telemetry()->reportEvent("audiosenderStartRecording");
            // it's first call onAudioSourceData after startRecording was called
            // clear resultBuffer, because now it stores sound of previous record
            resultBuffer.clear();
            resultBuffer.resize(permanentBuffer->getCapacity());

            // copy chunks from permanentBuffer to resultBuffer
            permanentBuffer->read(resultBuffer.data(), resultBuffer.size());
        }
        resultBuffer.push_back(chunk);
    }

    permanentBuffer->write(&chunk, 1);
}
