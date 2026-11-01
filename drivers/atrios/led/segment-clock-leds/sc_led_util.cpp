#include "sc_led_util.h"
#include "sc_led_pattern.h"

#include <yandex_io/modules/leds/led_manager/led_animator.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_conductor.h>

#include <yandex_io/libs/terminate_waiter/terminate_waiter.h>

#include <fstream>

using namespace quasar;

int segment_clock_led_util(int argc, char** argv, std::shared_ptr<ScLedController> ledController)
{
    TerminateWaiter waiter;

    LedAnimator animator(ScLedPattern::getIdlePattern(ledController));

    std::shared_ptr<ScLedPattern> pattern;

    if (argc >= 3 && std::string(argv[2]) == "-b")
    {
        const std::string fileName = argv[1];
        std::ifstream patternFile(fileName, std::ios::in | std::ios::binary);
        if (patternFile.is_open()) {
            patternFile >> std::noskipws;

            std::vector<uint8_t> values;
            std::copy(
                std::istream_iterator<uint8_t>(patternFile),
                std::istream_iterator<uint8_t>(),
                std::back_inserter(values));

            try {
                pattern = ScLedPattern::loadFromBinary(fileName, values, ledController);
            } catch (const std::exception& e) {
                throw std::runtime_error("Cannot load binary pattern from file '" + fileName + "': " + e.what());
            }
        } else {
            throw std::runtime_error("Cannot open binary pattern file '" + fileName + "'");
        }
    } else {
        pattern = ScLedPattern::loadFromFile(argv[1], ledController);
    }

    animator.play(std::make_shared<DefaultAnimationConductor>(pattern, SubstitutionType::FOREGROUND));

    waiter.wait();
    return 0;
}
