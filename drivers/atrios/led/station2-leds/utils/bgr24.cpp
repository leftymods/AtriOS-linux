#include "bgr24.h"

const BGR24 BGR24::black{BGR_BLACK, BGR_BLACK, BGR_BLACK};
const BGR24 BGR24::white{BGR_WHITE, BGR_WHITE, BGR_WHITE};

BGR24& BGR24::operator=(std::uint8_t brightness) {
    r = brightness;
    g = brightness;
    b = brightness;
    return *this;
}

BGR24& BGR24::operator=(float brightness) {
    return operator=((std::uint8_t)(brightness * BGR_WHITE)); // NOLINT(misc-unconventional-assign-operator)
}
