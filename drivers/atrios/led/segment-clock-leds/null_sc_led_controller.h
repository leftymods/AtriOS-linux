#pragma once

#include "sc_led_controller.h"

namespace quasar {

    class NullScLedController: public ScLedController {
    public:
        virtual void drawFrame(ScLedFrame /* frame */) override{};

        virtual int getLedCount() const override {
            return 0;
        }
    };

}
