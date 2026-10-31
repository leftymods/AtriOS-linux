#pragma once

#include "hci.h"

#include <yandex_io/libs/logging/logging.h>

namespace telink {

    template <class T, class... Commands>
    class HciCommandHandler: public HciListener {
    private:
        HciStatus onCommand(uint16_t msgType, std::span<const uint8_t> payload) override {
            return handleCommandImpl<Commands...>(msgType, payload);
        }

        template <class Command, class... Rest>
        HciStatus handleCommandImpl(uint16_t msgType, std::span<const uint8_t> payload) {
            if (msgType == Command::TYPE) {
                Command cmd;

                if (!cmd.deserialize(payload)) {
                    return HciStatus::BadMsg;
                }

                return static_cast<T*>(this)->handle(std::move(cmd));
            }

            if constexpr (sizeof...(Rest) > 0) {
                return handleCommandImpl<Rest...>(msgType, payload);
            }

            return HciStatus::UnhandledCommand;
        }
    };

} // namespace telink
