#pragma once

#include "sc_led_controller.h"

#include <memory>

int segment_clock_led_util(int argc, char** argv, std::shared_ptr<ScLedController> ledController);
