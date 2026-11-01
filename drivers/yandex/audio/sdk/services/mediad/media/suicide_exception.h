#pragma once

#include <stdexcept>

namespace quasar {

    class SuicideException: public std::runtime_error {
        using std::runtime_error::runtime_error;
    };

} // namespace quasar
