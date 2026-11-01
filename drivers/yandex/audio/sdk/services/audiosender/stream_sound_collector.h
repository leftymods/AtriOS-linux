#pragma once

#include "audio_config.h"
#include "audio_source_adapter.h"

#include <json/value.h>

#include <websocketpp/server.hpp>
#include <websocketpp/config/asio_no_tls.hpp>

#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace AudioSender {

    struct CommandResult;

    class StreamSoundCollector
        : public AudioSourceAdapter::Listener,
          public std::enable_shared_from_this<StreamSoundCollector> {
    public:
        using SharedPtr = std::shared_ptr<StreamSoundCollector>;
        using WeakPtr = std::weak_ptr<StreamSoundCollector>;

    public:
        StreamSoundCollector(AudioConfig::SharedPtr config,
                             AudioSourceAdapter::SharedPtr quasarAudioSourceAdapter,
                             std::shared_ptr<websocketpp::server<websocketpp::config::asio>> endpoint);

        CommandResult startRecording(websocketpp::connection_hdl handler,
                                     const Json::Value& request,
                                     const std::unordered_set<std::string>& dumpingChannels);
        CommandResult stopRecording();

        // AudioSourceAdapter::Listener implementation
        void onData(const AudioSourceAdapter::ChannelToBuffer& channelToBuffer) override;

    private:
        using ChannelToBufferList = std::map<std::string, std::list<SpeechKit::SoundBuffer::SharedPtr>>;

    private:
        ChannelToBufferList getSoundToSend();
        bool sendSound(const std::unordered_set<std::string>& dumpingChannels);
        std::string sendJson(const Json::Value& payload);

    private:
        ChannelToBufferList channelToBufferList;
        std::shared_ptr<websocketpp::server<websocketpp::config::asio>> endpoint;
        websocketpp::connection_hdl connection;

        std::string refMessageId;

        std::atomic_bool isRecording{false};
        std::mutex bufferMutex;
        std::thread periodicSendThread;
        size_t bufferMaxCapacity;
        size_t bufferCurrentCapacity;

        AudioSourceAdapter::SharedPtr quasarAudioSourceAdapter;
    };

} // namespace AudioSender
