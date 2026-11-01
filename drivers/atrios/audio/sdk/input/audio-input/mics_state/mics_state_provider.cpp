#include "mics_state_provider.h"

namespace YandexIO {

    MicsStateProvider::~MicsStateProvider() = default;

    void MicsStateProvider::addListener(std::weak_ptr<IMicsStateListener> listener) {
        listeners_.push_back(std::move(listener));
    }

    void MicsStateProvider::notifyMuted() {
        for (auto& weakListener : listeners_) {
            if (auto listener = weakListener.lock()) {
                listener->onMuted();
            }
        }
    }

    void MicsStateProvider::notifyUnmuted() {
        for (auto& weakListener : listeners_) {
            if (auto listener = weakListener.lock()) {
                listener->onUnmuted();
            }
        }
    }

} /* namespace YandexIO */
