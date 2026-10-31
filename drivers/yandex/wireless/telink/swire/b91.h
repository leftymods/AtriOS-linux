#pragma once

#include "i_swire.h"

#include <string>

namespace telink {

    class B91 {
    public:
        B91(std::shared_ptr<ISwire> swire);

        bool sync();

        void reboot();

        void runBinary(const std::string& bin);

    private:
        std::shared_ptr<ISwire> swire_;
    };

} // namespace telink
