#include "stream_sound_collector.h"

#include "command_result.h"
#include "command_server.h"

#include <yandex_io/libs/logging/logging.h>

#include <chrono>

YIO_DEFINE_LOG_MODULE("audio_sender");

using namespace AudioSender;

static constexpr char COMMAND_NAME_STREAM_SOUND[] = "streamSound";
static constexpr int SOUND_SEND_INTERVAL_MS = 1000;
static constexpr int STREAMING_BUFFER_MAX_CAPACITY = 2000000;

StreamSoundCollector::StreamSoundCollector(AudioConfig::SharedPtr /* config */,
                                           AudioSourceAdapter::SharedPtr quasarAudioSourceAdapter,
                                           std::shared_ptr<websocketpp::server<websocketpp::config::asio>> endpoint)
    : endpoint(endpoint)
    , quasarAudioSourceAdapter(quasarAudioSourceAdapter)
{
    bufferMaxCapacity = STREAMING_BUFFER_MAX_CAPACITY;
}

CommandResult StreamSoundCollector::startRecording(websocketpp::connection_hdl handler,
                                                   const Json::Value& request,
                                                   const std::unordered_set<std::string>& dumpingChannels) {
    if (isRecording.load()) {
        return {false, "Recording is already in progress"};
    }

    this->connection = handler;
    this->refMessageId = request.get("messageId", "unspecified").asString();

    channelToBufferList.clear();
    quasarAudioSourceAdapter->subscribe(shared_from_this(), dumpingChannels);
    isRecording.store(true);

    const auto soundInfo = quasarAudioSourceAdapter->getSoundInfo();
    Json::Value payload;
    payload["result"] = true;
    payload["command"] = request.get("command", "error").asString();
    payload["soundInfo"]["sampleRate"] = soundInfo.getSampleRate();
    payload["soundInfo"]["sampleSize"] = soundInfo.getSampleSize();
    payload["soundInfo"]["channelCount"] = soundInfo.getChannelCount();
    payload["refMessageId"] = refMessageId;
    std::string err = sendJson(payload);
    if (!err.empty()) {
        stopRecording();
        return {false, err};
    }

    periodicSendThread = std::thread([this, dumpingChannels] {
        while (this->isRecording.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{SOUND_SEND_INTERVAL_MS});
            if (!sendSound(dumpingChannels)) {
                this->stopRecording();
            }
        }
    });

    return true;
}

CommandResult StreamSoundCollector::stopRecording() {
    bool expected = true;
    if (!isRecording.compare_exchange_strong(expected, false)) {
        return true;
    }

    quasarAudioSourceAdapter->unsubscribe(shared_from_this());
    if (periodicSendThread.joinable()) {
        periodicSendThread.join();
    }

    return true;
}

void StreamSoundCollector::onData(const AudioSourceAdapter::ChannelToBuffer& channelToBuffer) {
    if (!isRecording.load()) {
        return;
    }

    if (bufferMaxCapacity > 0 && bufferMaxCapacity <= bufferCurrentCapacity) {
        YIO_LOG_ERROR_EVENT("StreamSoundCollector.MaxBufferCapacityReached", "capacity: " << bufferCurrentCapacity);
        isRecording.store(false);
        return;
    }

    std::lock_guard<std::mutex> lock(bufferMutex);
    for (const auto& pair : channelToBuffer) {
        channelToBufferList[pair.first].push_back(pair.second);
        bufferCurrentCapacity += pair.second->getData().size();
    }
}

StreamSoundCollector::ChannelToBufferList StreamSoundCollector::getSoundToSend() {
    ChannelToBufferList ret;
    std::lock_guard<std::mutex> lock(bufferMutex);
    std::swap(channelToBufferList, ret);
    return ret;
}

bool StreamSoundCollector::sendSound(const std::unordered_set<std::string>& dumpingChannels) {
    auto sound = getSoundToSend();

    if (sound.empty()) {
        return true;
    }

    for (const auto& channel : dumpingChannels) {
        if (sound.find(channel) == sound.end()) {
            continue;
        }

        size_t size = 0;
        for (const auto& soundBuffer : sound[channel]) {
            size += soundBuffer->getData().size();
        }

        Json::Value payload;
        payload["result"] = true;
        payload["command"] = COMMAND_NAME_STREAM_SOUND;
        payload["channel"] = channel;
        payload["size"] = size;
        payload["refMessageId"] = refMessageId;
        std::string err = sendJson(payload);
        if (!err.empty()) {
            YIO_LOG_ERROR_EVENT("StreamSoundCollector.SendingMessageError", err);
            return false;
        }

        for (const auto& soundBuffer : sound[channel]) {
            websocketpp::lib::error_code ec;
            endpoint->send(connection,
                           soundBuffer->getData().data(),
                           soundBuffer->getData().size(),
                           websocketpp::frame::opcode::binary,
                           ec);
            if (ec) {
                YIO_LOG_ERROR_EVENT("StreamSoundCollector.SendingDataError", ec.message());
                return false;
            }
        }
    }

    return true;
}

std::string StreamSoundCollector::sendJson(const Json::Value& payload) {
    std::string stringPayload = Json::FastWriter().write(payload);
    websocketpp::lib::error_code ec;
    endpoint->send(connection, stringPayload, websocketpp::frame::opcode::text, ec);
    return ec ? ec.message() : "";
}
