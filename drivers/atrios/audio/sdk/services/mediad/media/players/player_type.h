#pragma once

#include <string>

namespace quasar {

    enum class PlayerType {
        YANDEX_MUSIC = 0,
        YANDEX_RADIO = 1,
        NONE /* keep this value last */
    };

    std::string playerTypeToString(PlayerType type);

} // namespace quasar
