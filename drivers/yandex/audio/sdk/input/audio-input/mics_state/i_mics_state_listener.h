#pragma once

namespace YandexIO {

    class IMicsStateListener {
    public:
        IMicsStateListener() = default;
        virtual ~IMicsStateListener();

        virtual void onMuted() = 0;
        virtual void onUnmuted() = 0;
    };

} /* namespace YandexIO */
