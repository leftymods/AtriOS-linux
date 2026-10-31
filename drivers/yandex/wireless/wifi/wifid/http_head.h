#pragma once

#include <string>

namespace quasar {
    struct HeadResponse {
        int responseCode{0};
    };

    HeadResponse httpHead(const std::string& host, const std::string& path, const std::string& userAgent);
} // namespace quasar
