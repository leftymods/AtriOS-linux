#pragma once

#include <cstdint>
#include <vector>

static const int BGR_BLACK = 0;
static const int BGR_WHITE = 255;

struct BGR24 {
    std::uint8_t b;
    std::uint8_t g;
    std::uint8_t r;

    static const BGR24 black;
    static const BGR24 white;

    BGR24& operator=(std::uint8_t brightness);
    BGR24& operator=(float brightness);

} __attribute__((__packed__));

static_assert(sizeof(BGR24) == 3);
static_assert(alignof(BGR24) == 1);

using Frame = std::vector<BGR24>;
