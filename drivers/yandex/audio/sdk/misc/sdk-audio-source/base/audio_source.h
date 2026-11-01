#pragma once

#include <yandex_io/sdk/audio_source/i_audio_source.h>

#include <list>
#include <mutex>

namespace YandexIO {
    class LocalAudioSourceClient;

    /**
     * @brief Base implementation that provide data to local AudioSourceClients
     *        and then forward it to pushDataImpl (to provide data to other processes)
     */
    class AudioSource: public IAudioSource {
    public:
        void pushData(ChannelsData data) final;
        /**
         * @brief Create and save local AudioSourceClient (that will receive)
         *        ChannelsData directly
         */
        std::shared_ptr<IAudioSourceClient> createClient() final;

    protected:
        virtual void pushDataImpl(ChannelsData data);

    private:
        std::mutex mutex_;
        std::list<std::weak_ptr<LocalAudioSourceClient>> clients_;
    };

} // namespace YandexIO
