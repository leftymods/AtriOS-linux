#include "doa_listener.h"

#include <util/system/yassert.h>

#include <cmath>
#include <cstdlib>

namespace YandexIO {

    DoaListener::DoaListener(YandexIO::DoaListener::OnDoaChanged onDoaChanged)
        : onDoaChanged_(std::move(onDoaChanged))
    {
        Y_VERIFY(onDoaChanged_ != nullptr);
    }

    void DoaListener::onAudioData(const ChannelsData& data) {
        // TODO: Find not main channel, but requested? (by channel name);
        const auto it = std::find_if(data.cbegin(), data.cend(), [](const auto& channel) {
            return channel.isForRecognition;
        });
        if (it == data.cend()) {
            return;
        }
        if (!it->meta.doaAngle.has_value()) {
            return;
        }
        const double doa = *it->meta.doaAngle;
        if (std::abs(doa_ - doa) > 0.0001) {
            doa_ = doa;
            onDoaChanged_(doa_);
        }
    }

} /* namespace YandexIO */
