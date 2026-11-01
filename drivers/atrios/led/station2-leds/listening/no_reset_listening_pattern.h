#pragma once

#include <yandex_io/modules/leds/led_patterns/listening_pattern.h>

#include <memory>

/**
 * AnimationComposition resets all patterns when any of Animation is end. ListeningPattern should be reset explicitly
 * TODO: replace with IndependentAnimationComposition - Composition that does not reset both animation but only that
 *  is ended
 */
class NoResetListeningPattern: public quasar::ListeningPattern {
public:
    NoResetListeningPattern(std::shared_ptr<LedPattern> base, double doaAngle, bool invertRotation);

    void resetAnimation() override;
    void manualResetAnimation();
};
