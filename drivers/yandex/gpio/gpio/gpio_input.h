#pragma once

#include "gpio.h"

#include <vector>

namespace quasar {

    class GPIOInput {
    public:
        /**
     * The lifetime of returned GPIO* is equal to lifetime of GPIOInput object
     */
        virtual std::vector<const GPIO*> getHandles() const = 0;
        virtual void processEvent() = 0;
        virtual void reload() = 0;

        virtual ~GPIOInput() = default;
    };

} // namespace quasar
