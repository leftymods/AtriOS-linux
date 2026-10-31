#include "rcu_button.h"

#include <set>
#include <map>

namespace rcu_button {

    namespace {

        std::map<int, Key> keysByCodes = {
            {4, Key::BACK},
            {19, Key::NAV_UP},
            {20, Key::NAV_DOWN},
            {21, Key::NAV_LEFT},
            {22, Key::NAV_RIGHT},
            {23, Key::MAIN},
            {84, Key::ACTIVATE},
            {113, Key::VOLUME_MUTE},
            {114, Key::VOLUME_DOWN},
            {115, Key::VOLUME_UP}};

    } // namespace

    std::optional<Key> findKey(int keyCode) {
        auto it = keysByCodes.find(keyCode);
        if (it != keysByCodes.end()) {
            return it->second;
        }
        return std::nullopt;
    }

} // namespace rcu_button
