#pragma once

#include "i_audio_source_client.h"

#include <yandex_io/libs/ipc/i_ipc_factory.h>

#include <memory>

namespace YandexIO {
    class IAudioSource {
    public:
        virtual ~IAudioSource();
        /**
         * @brief Push channels data to listeners
         */
        virtual void pushData(ChannelsData data) = 0;

        /**
         * @brief Return audio client that will receive ChannelsData
         *        directly from audio source (in this process)
         */
        virtual std::shared_ptr<IAudioSourceClient> createClient() = 0;
    };

    std::shared_ptr<IAudioSource> createAudioSource(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory);

} /* namespace YandexIO */
