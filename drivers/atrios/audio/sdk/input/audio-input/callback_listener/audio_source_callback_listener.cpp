#include "audio_source_callback_listener.h"

#include <util/system/yassert.h>

namespace YandexIO {

    AudioSourceCallbackListener::AudioSourceCallbackListener(OnAudioData onAudioDataCb)
        : onAudioData_(std::move(onAudioDataCb))
    {
        Y_VERIFY(onAudioData_ != nullptr);
    }

    void AudioSourceCallbackListener::onAudioData(const ChannelsData& data) {
        onAudioData_(data);
    }

} /* namespace YandexIO */
