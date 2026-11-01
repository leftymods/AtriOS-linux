#include "utils.h"

#include <algorithm>
#include <cmath>

namespace quasar {

    std::tuple<float, float, float> rgbToHsv(float r, float g, float b) {
        r = std::clamp(r, 0.0f, 1.0f);
        g = std::clamp(g, 0.0f, 1.0f);
        b = std::clamp(b, 0.0f, 1.0f);

        float max = std::max(r, std::max(g, b));
        float min = std::min(r, std::min(g, b));

        float v = max;
        float c = max - min;

        float h;
        if (c == 0.0f) {
            h = 0.0f;
        } else if (v == r) {
            h = 60.0f * (0.0f + (g - b) / c);
        } else if (v == g) {
            h = 60.0f * (2.0f + (b - r) / c);
        } else {
            h = 60.0f * (4.0f + (r - g) / c);
        }

        if (h < 0.0f) {
            h += 360.0f;
        } else if (h > 360.0f) {
            h -= 360.0f;
        }

        float s = 0.0f;
        if (v != 0.0f) {
            s = c / v;
        }

        return std::make_tuple(h, s, v);
    }

    std::tuple<float, float, float> hsvToRgb(float h, float s, float v) {
        if (h < 0.0f) {
            h += 360.0f;
        } else if (h > 360.0f) {
            h -= 360.0f;
        }

        s = std::clamp(s, 0.0f, 1.0f);
        v = std::clamp(v, 0.0f, 1.0f);

        float c = v * s;

        float hi = h / 60.0f;

        float x = c * (1.0f - std::fabs(std::fmod(hi, 2.0f) - 1.0f));

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;

        switch ((int)std::floor(hi)) {
            case 0:
                r = c;
                g = x;
                break;
            case 1:
                r = x;
                g = c;
                break;
            case 2:
                g = c;
                b = x;
                break;
            case 3:
                g = x;
                b = c;
                break;
            case 4:
                r = x;
                b = c;
                break;
            case 5:
                r = c;
                b = x;
                break;
            default:
                break;
        }

        float m = v - c;
        r += m;
        g += m;
        b += m;

        return std::tuple(r, g, b);
    }

} // namespace quasar
