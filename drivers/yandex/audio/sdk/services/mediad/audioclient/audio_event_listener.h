#pragma once

#include <yandex_io/protos/model_objects.pb.h>

namespace quasar {
    class AudioEventListener {
    public:
        virtual ~AudioEventListener() = default;
        virtual void onAudioEvent(const proto::AudioClientEvent& event) = 0;
    };
} // namespace quasar
