#include "no_reset_listening_pattern.h"

NoResetListeningPattern::NoResetListeningPattern(std::shared_ptr<LedPattern> base, double doaAngle, bool invertRotation)
    : ListeningPattern(base, doaAngle, invertRotation)
{
}

void NoResetListeningPattern::resetAnimation() {
    // no action
}

void NoResetListeningPattern::manualResetAnimation() {
    ListeningPattern::resetAnimation();
}
