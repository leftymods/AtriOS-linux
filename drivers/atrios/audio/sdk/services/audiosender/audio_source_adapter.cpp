#include "audio_source_adapter.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <algorithm>

YIO_DEFINE_LOG_MODULE("audio_sender");

using namespace AudioSender;

using namespace SpeechKit;

AudioSourceAdapter::AudioSourceAdapter() = default;

void AudioSourceAdapter::subscribe(Listener::WeakPtr wlistener, const std::unordered_set<std::string>& channelNames) {
    if (auto listener = wlistener.lock()) {
        std::stringstream ss;
        for (const auto& channel : channelNames) {
            ss << "[" << channel << "]";
        }
        YIO_LOG_DEBUG("AudioSourceAdapter::subscribe. Listener " << listener << " channelNames: " << ss.str());
        std::lock_guard<std::mutex> lock(listenersMutex);
        weakListeners[wlistener] = channelNames;
    }
}

void AudioSourceAdapter::unsubscribe(Listener::WeakPtr wlistener) {
    std::stringstream listenerStream;
    if (auto listener = wlistener.lock()) {
        listenerStream << listener.get();
    } else {
        listenerStream << "expired";
    }
    YIO_LOG_DEBUG("AudioSourceAdapter::unsubscribe. Listener " << listenerStream.str());
    std::lock_guard<std::mutex> lock(listenersMutex);
    weakListeners.erase(wlistener);
}

void AudioSourceAdapter::subscribe(AudioSourceListener::WeakPtr wlistener) {
    if (auto listener = wlistener.lock()) {
        // handle audio source
        bool inserted = false;
        {
            std::lock_guard<std::mutex> lock(skListenersMutex);
            inserted = weakSkListeners.insert(wlistener).second;
        }
        if (inserted) {
            listener->onAudioSourceStarted(shared_from_this());
        }
    }
}

void AudioSourceAdapter::handleAudioData(const ChannelToBuffer& channels, const std::string& mainChannel) {
    pushToSkListeners(channels, mainChannel);
    pushToListeners(channels);
}

void AudioSourceAdapter::pushToListeners(const ChannelToBuffer& channels) {
    // distribute sound by internal listeners
    std::lock_guard<std::mutex> lock(listenersMutex);
    for (auto& kv : weakListeners) {
        auto& wlistener = kv.first;
        const auto& expectedChannelNames = kv.second;
        if (auto listener = wlistener.lock()) {
            ChannelToBuffer expectedChunks;
            for (const auto& capturedChunk : channels) {
                if (expectedChannelNames.count(capturedChunk.first) > 0) {
                    expectedChunks[capturedChunk.first] = capturedChunk.second;
                }
            }
            listener->onData(expectedChunks);
        }
    }
}

void AudioSourceAdapter::pushToSkListeners(const ChannelToBuffer& channels, const std::string& mainChannel) {
    std::lock_guard<std::mutex> lock(skListenersMutex);
    if (!weakSkListeners.empty()) {
        // distribute sound by speechkit listeners
        const auto speechkitBufferIter = channels.find(mainChannel);
        Y_VERIFY(speechkitBufferIter != channels.end() && "There is no such channel in received audio data");
        for (auto& wlistener : weakSkListeners) {
            if (auto listener = wlistener.lock()) {
                listener->onAudioSourceData(shared_from_this(), speechkitBufferIter->second);
            }
        }
    }
}

void AudioSourceAdapter::unsubscribe(AudioSourceListener::WeakPtr wlistener) {
    std::stringstream listenerStream;
    if (auto listener = wlistener.lock()) {
        listenerStream << listener.get();
    } else {
        listenerStream << "expired";
    }
    YIO_LOG_DEBUG("AudioSourceAdapter::unsubscribe. AudioSourceListener " << listenerStream.str());
    std::lock_guard<std::mutex> lock(skListenersMutex);
    weakSkListeners.erase(wlistener);
}
