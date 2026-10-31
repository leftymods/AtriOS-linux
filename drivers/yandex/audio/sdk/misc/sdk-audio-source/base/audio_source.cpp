#include "audio_source.h"

#include <optional>

namespace YandexIO {
    class LocalAudioSourceClient: public IAudioSourceClient {
    public:
        void onAudioData(const ChannelsData& data) {
            std::unique_lock lock(mutex_);
            if (!started_) {
                return;
            }
            auto type = type_;
            auto listeners = listeners_;
            lock.unlock();

            if (!type.has_value()) {
                return;
            }
            /* Simply forward all channels if it is requested */
            if (type.value() == RequestChannelType::ALL) {
                for (auto& listener : listeners) {
                    if (auto slistener = listener.lock()) {
                        slistener->onAudioData(data);
                    }
                }
                return;
            }

            /* Find one requested channel */
            auto it = data.cend();
            switch (type.value()) {
                case RequestChannelType::MAIN: {
                    it = std::find_if(data.cbegin(), data.cend(), [](const ChannelData& channel) {
                        return channel.isForRecognition;
                    });
                    break;
                }
                case RequestChannelType::RAW: {
                    it = std::find_if(data.cbegin(), data.cend(), [](const ChannelData& channel) {
                        return channel.type == ChannelData::Type::RAW;
                    });
                    break;
                }
                case RequestChannelType::VQE: {
                    it = std::find_if(data.cbegin(), data.cend(), [](const ChannelData& channel) {
                        return channel.type == ChannelData::Type::VQE;
                    });
                    break;
                }
                case RequestChannelType::ALL: {
                    /* already handled */
                    break;
                }
            }
            if (it == data.cend()) {
                return;
            }
            for (auto& listener : listeners) {
                if (auto slistener = listener.lock()) {
                    slistener->onAudioData({*it});
                }
            }
        }

        void subscribeToChannels(RequestChannelType type) override {
            std::scoped_lock guard(mutex_);
            type_ = type;
        }

        void unsubscribeFromChannels() override {
            std::scoped_lock guard(mutex_);
            type_.reset();
        }
        void start() override {
            std::scoped_lock guard(mutex_);
            started_ = true;
        };
        void addListener(std::weak_ptr<Listener> listener) override {
            std::scoped_lock guard(mutex_);
            listeners_.push_back(listener);
        }

    private:
        bool started_{false};
        std::mutex mutex_;
        std::optional<RequestChannelType> type_;
        std::list<std::weak_ptr<Listener>> listeners_;
    };

    void AudioSource::pushData(ChannelsData data) {
        std::unique_lock lock(mutex_);
        auto clients = clients_;
        lock.unlock();
        for (auto& client : clients) {
            if (auto sclient = client.lock()) {
                sclient->onAudioData(data);
            }
        }

        // remove local channels because non-local clients dont need them
        data.remove_if([](const ChannelData& data) {
            return data.local;
        });

        pushDataImpl(std::move(data));
    }

    void AudioSource::pushDataImpl(ChannelsData /* data */) {
        // do nothing
    }

    std::shared_ptr<IAudioSourceClient> AudioSource::createClient() {
        auto client = std::make_shared<LocalAudioSourceClient>();
        {
            std::scoped_lock guard(mutex_);
            clients_.push_back(client);
        }
        return client;
    }

} // namespace YandexIO
