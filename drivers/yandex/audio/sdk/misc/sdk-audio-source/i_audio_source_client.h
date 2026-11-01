#pragma once

#include "defines.h"

#include <yandex_io/libs/ipc/i_ipc_factory.h>

#include <memory>

namespace YandexIO {
    class IAudioSourceClient {
    public:
        class Listener {
        public:
            virtual ~Listener();
            virtual void onAudioData(const ChannelsData& data) = 0;
        };

        virtual ~IAudioSourceClient();

        /**
         *  @brief Choose channel type Listeners will receive via this IAudioClient instance
         *  @note Should be called after "start" method
         */
        virtual void subscribeToChannels(RequestChannelType type) = 0;

        virtual void unsubscribeFromChannels() = 0;

        /**
         * @TODO: proper description // fixme: do we need it ?
         */
        virtual void start() = 0;

        /**
         * @note Should be called before start
         */
        virtual void addListener(std::weak_ptr<Listener> listener) = 0;
    };

    /*
     * @brief Create IAudioSourceClient that will receive ChannelsData
     *        via interprocess bus
     */
    std::shared_ptr<IAudioSourceClient> createAudioSourceClient(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory);

} /* namespace YandexIO */
