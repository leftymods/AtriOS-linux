#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace quasar {

    class AudioWriter {
    public:
        virtual ~AudioWriter();

        virtual bool write(const std::vector<int16_t>& data) = 0;

        virtual void tryRecover();
        virtual std::string getError() const;
    };

} // namespace quasar
