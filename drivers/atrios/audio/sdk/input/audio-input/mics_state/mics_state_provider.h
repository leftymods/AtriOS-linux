#pragma once

#include "i_mics_state_listener.h"

#include <list>
#include <memory>

namespace YandexIO {

    class MicsStateProvider {
    public:
        MicsStateProvider() = default;
        virtual ~MicsStateProvider();
        void addListener(std::weak_ptr<IMicsStateListener> listener);

    protected:
        void notifyMuted();
        void notifyUnmuted();

    private:
        std::list<std::weak_ptr<IMicsStateListener>> listeners_;
    };

} /* namespace YandexIO */
