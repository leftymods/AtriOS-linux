#pragma once

#include <json/json.h>

#include <speechkit/AudioSource.h>

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_set>

namespace AudioSender {

    class AudioSourceAdapter: public SpeechKit::AudioSource,
                              public std::enable_shared_from_this<AudioSourceAdapter> {
    public:
        using SharedPtr = std::shared_ptr<AudioSourceAdapter>;
        using ChannelToBuffer = std::map<std::string, SpeechKit::SoundBuffer::SharedPtr>;

        class Listener {
        public:
            using WeakPtr = std::weak_ptr<Listener>;
            virtual ~Listener() = default;
            virtual void onData(const ChannelToBuffer& chunks) = 0;
        };

    public:
        /* Api to implement */
        virtual std::unordered_set<std::string> getAvailableChannelNames() const = 0;
        int getBufferCaptureTimeout() const override = 0;
        const SpeechKit::SoundInfo& getSoundInfo() const override = 0;

    public:
        AudioSourceAdapter();
        void subscribe(Listener::WeakPtr listener, const std::unordered_set<std::string>& channelNames);
        void unsubscribe(Listener::WeakPtr listener);

        void subscribe(AudioSourceListener::WeakPtr listener) final;
        void unsubscribe(AudioSourceListener::WeakPtr listener) final;

    protected:
        /* forward data to listeners */
        void handleAudioData(const ChannelToBuffer& channels, const std::string& mainChannel);
        void pushToListeners(const ChannelToBuffer& channels);
        void pushToSkListeners(const ChannelToBuffer& channels, const std::string& mainChannel);

    private:
        using WeakListener = Listener::WeakPtr;
        using ListenersComparator = std::owner_less<WeakListener>;
        std::map<WeakListener, std::unordered_set<std::string>, ListenersComparator> weakListeners;
        mutable std::mutex listenersMutex;

        using WeakSkListener = AudioSourceListener::WeakPtr;
        using SkListenersComparator = std::owner_less<WeakSkListener>;
        std::set<WeakSkListener, SkListenersComparator> weakSkListeners;
        mutable std::mutex skListenersMutex;
    };

} // namespace AudioSender
