#pragma once

#include <span>
#include <vector>

namespace telink {

    template <uint16_t Type>
    struct HciCommand {
        static constexpr uint16_t TYPE = Type;

        std::vector<uint8_t> serialize() const {
            return {};
        }

        bool deserialize(std::span<const uint8_t> payload) {
            return payload.empty();
        }
    };

} // namespace telink
