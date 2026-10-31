#include "callback_mics_state_listener.h"

#include <util/system/yassert.h>

namespace YandexIO {

    CallbackMicsStateListener::CallbackMicsStateListener(OnMicsStateChanged onMicsStateChanged)
        : onMicsStateChanged_(std::move(onMicsStateChanged))
    {
        Y_VERIFY(onMicsStateChanged_ != nullptr);
    }

    void CallbackMicsStateListener::onMuted() {
        onMicsStateChanged_(true);
    }

    void CallbackMicsStateListener::onUnmuted() {
        onMicsStateChanged_(false);
    }

} /* namespace YandexIO */
