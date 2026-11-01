#include "stream_app_src.h"

using namespace quasar;

std::string StreamAppSrc::getName() const {
    return "stream-src";
}

std::vector<uint8_t> StreamAppSrc::pullData() {
    std::vector<uint8_t> result;
    if (endOfStream_) {
        return result;
    }
    queue_.pop(result);
    if (result.size() == 0) {
        endOfStream_ = true;
    }
    return result;
}

void StreamAppSrc::pushData(std::vector<uint8_t> buffer) {
    queue_.push(std::move(buffer));
}

void StreamAppSrc::setDataEnd() {
    queue_.push(std::vector<uint8_t>());
}
