#include "perlin_noise.h"

#include <util/system/yassert.h>

#include <algorithm>
#include <array>

namespace {
    std::vector<Vector2> generateGradients(int seed, int size) {
        std::uniform_real_distribution<float> angleDistribution{0, 2 * 3.14159265358979};
        std::minstd_rand generator(seed);

        std::vector<Vector2> result;
        result.reserve(size);
        for (int i = 0; i < size; ++i) {
            float angle = angleDistribution(generator);
            result.emplace_back(std::sin(angle), std::cos(angle));
        }

        return result;
    }
} // namespace

PerlinNoise::PerlinNoise(int seed, int gridStep, int gridCellsWidth, int gridCellsHeight, Interpolation interpolation)
    : gradients_(generateGradients(seed, gridCellsWidth * gridCellsHeight))
    , gridStep_(gridStep)
    , gridCellsWidth_(gridCellsWidth)
    , gridCellsHeight_(gridCellsHeight)
    , width_(gridStep * gridCellsWidth)
    , height_(gridStep * gridCellsHeight)
    , interpolation_(interpolation)
{
    Y_VERIFY(gridStep_ > 0);
    Y_VERIFY(gridCellsWidth_ > 0);
    Y_VERIFY(gridCellsHeight_ > 0);
}

int PerlinNoise::height() const {
    return height_;
}

int PerlinNoise::width() const {
    return width_;
}

float PerlinNoise::getNoise(int x, int y) {
    x = x % width_;
    y = y % height_;
    x = (x < 0) ? (x + width_) : x;
    y = (y < 0) ? (y + height_) : y;

    int x0 = x - x % gridStep_;
    int y0 = y - y % gridStep_;
    int x1 = (x0 + gridStep_) % width_;
    int y1 = (y0 + gridStep_) % height_;

    float dx0 = (float)(x - x0) / gridStep_;
    float dy0 = (float)(y - y0) / gridStep_;
    float dx1 = dx0 - 1.0;
    float dy1 = dy0 - 1.0;

    float n00 = getGridGradient(x0 / gridStep_, y0 / gridStep_) * Vector2(dx0, dy0);
    float n01 = getGridGradient(x0 / gridStep_, y1 / gridStep_) * Vector2(dx0, dy1);
    float n10 = getGridGradient(x1 / gridStep_, y0 / gridStep_) * Vector2(dx1, dy0);
    float n11 = getGridGradient(x1 / gridStep_, y1 / gridStep_) * Vector2(dx1, dy1);

    float interpolationY0 = interpolate(n00, n10, dx0);
    float interpolationY1 = interpolate(n01, n11, dx0);

    return interpolate(interpolationY0, interpolationY1, dy0);
}

const Vector2& PerlinNoise::getGridGradient(int x, int y) {
    return gradients_[y * gridCellsWidth_ + x];
}

float PerlinNoise::interpolate(float x, float y, float w) {
    switch (interpolation_) {
        case Interpolation::LINEAR:
            return (y - x) * w + x;
        case Interpolation::SMOOTH_STEP:
            return (y - x) * (3 - 2 * w) * w * w + x;
    }
}

float PerlinNoise::uniformize(float input) {
    // Quantiles were empirically generated, because Perlin noise's distribution is too complicated.
    // Values were generated with Interpolation::SMOOTH_STEP, so linear interpolation can differ
    static constexpr std::array<float, 101> quantiles = {{-0.695458, -0.463365, -0.427288, -0.400378, -0.378021, -0.358531, -0.340912,
                                                          -0.324897, -0.31004, -0.296132, -0.283079, -0.270724, -0.259009, -0.247877,
                                                          -0.237238, -0.22703, -0.217239, -0.207828, -0.198747, -0.189959, -0.181444,
                                                          -0.173226, -0.165285, -0.157651, -0.150245, -0.14308, -0.136124, -0.129393,
                                                          -0.12285, -0.116532, -0.110375, -0.104435, -0.0986684, -0.0931247, -0.0876437,
                                                          -0.0821741, -0.0767145, -0.0712405, -0.0657731, -0.0602933, -0.054817,
                                                          -0.0493429, -0.0438702, -0.0384068, -0.0329398, -0.0274852, -0.0220113,
                                                          -0.0165682, -0.0111012, -0.00566453, -0.000187576, 0.00522375, 0.0106427,
                                                          0.0161107, 0.0215681, 0.0270439, 0.0324906, 0.0379576, 0.0434166, 0.0488746,
                                                          0.0543322, 0.0597829, 0.0652411, 0.0706933, 0.0761629, 0.0816234, 0.0870864,
                                                          0.0925625, 0.0980951, 0.103847, 0.109779, 0.115918, 0.122258, 0.128806,
                                                          0.135563, 0.142554, 0.149774, 0.157222, 0.164885, 0.172832, 0.181025,
                                                          0.189516, 0.198279, 0.207326, 0.216742, 0.226547, 0.236804, 0.247627,
                                                          0.258987, 0.270894, 0.283407, 0.296666, 0.310823, 0.326003, 0.342401,
                                                          0.360321, 0.380181, 0.402804, 0.429944, 0.466257, 0.703731}};

    input = std::clamp(input, quantiles.front(), quantiles.back());
    auto lbound = std::lower_bound(quantiles.begin(), quantiles.end(), input);
    if (lbound == quantiles.begin()) {
        return 0.0;
    } else if (lbound == quantiles.end()) {
        // Mustn't happen
        return 0.0;
    } else {
        auto rightValue = *lbound;
        auto leftValue = *(lbound - 1);

        auto rightUniformValue = (float)(lbound - quantiles.begin()) / quantiles.size();
        auto leftUniformValue = (float)(lbound - quantiles.begin() - 1) / quantiles.size();

        auto rightCoef = input - leftValue;
        auto leftCoef = rightValue - input;
        auto totalCoef = rightCoef + leftCoef;

        return (rightCoef * rightUniformValue + leftCoef * leftUniformValue) / totalCoef;
    }
}
