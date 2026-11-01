#pragma once

#include <tuple>

namespace quasar {

    /**
 * Convert color between RGB and HSV colorspaces.
 * r, g, b components are in range [0.0, 1.0]
 * h is in range [0.0, 360.0], s, v are in range [0.0, 1.0]
 */
    std::tuple<float, float, float> rgbToHsv(float r, float g, float b);
    std::tuple<float, float, float> hsvToRgb(float h, float s, float v);

} // namespace quasar
