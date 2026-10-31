#pragma once

#include <string>

namespace AudioSender {

    std::string formatLocalTime(const std::string& format);
    std::string formatUnixTimestamp();

} // namespace AudioSender
