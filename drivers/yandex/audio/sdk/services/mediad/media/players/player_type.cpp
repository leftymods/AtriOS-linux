#include "player_type.h"

using namespace quasar;

std::string quasar::playerTypeToString(PlayerType type) {
    switch (type) {
        case PlayerType::YANDEX_MUSIC:
            return "yandex_music";
        case PlayerType::YANDEX_RADIO:
            return "yandex_radio";
        case PlayerType::NONE:
            return "none";
    }
    return "unknown";
}
