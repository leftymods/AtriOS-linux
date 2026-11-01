#pragma once

#include <string>

namespace AudioSender {

    struct CommandResult final {
        CommandResult(bool result = true, std::string message = "OK")
            : result(result)
            , message(std::move(message))
        {
        }
        explicit operator bool() const {
            return result;
        }

        bool result;
        std::string message;
    };

} // namespace AudioSender
