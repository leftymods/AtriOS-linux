#pragma once

#include <stdexcept>

namespace quasar {

    class AuthorizationFailedException: public std::runtime_error {
        using std::runtime_error::runtime_error;
    };

} // namespace quasar
