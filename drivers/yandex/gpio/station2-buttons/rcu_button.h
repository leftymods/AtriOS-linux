#pragma once

#include <optional>
#include <array>

namespace rcu_button {

    enum class Key {
        BACK,
        NAV_UP,
        NAV_DOWN,
        NAV_LEFT,
        NAV_RIGHT,
        MAIN,
        ACTIVATE,
        VOLUME_MUTE,
        VOLUME_DOWN,
        VOLUME_UP
    };

    std::optional<Key> findKey(int keyCode);

} // namespace rcu_button
