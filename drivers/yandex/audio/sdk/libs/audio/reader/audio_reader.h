#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <span>

namespace quasar {

    class AudioReader {
    public:
        virtual ~AudioReader();

        virtual bool read(std::vector<uint8_t>& data, int framesCount) = 0;
        virtual bool read(std::span<std::byte> data, int framesCount) = 0;
        virtual int getFrameRate() const = 0;

        virtual void tryRecover();

        virtual std::string getError() const;
    };

} // namespace quasar
