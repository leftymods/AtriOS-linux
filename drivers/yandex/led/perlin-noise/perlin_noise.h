#pragma once

#include <cmath>
#include <random>
#include <vector>

struct Vector2 {
    float x;
    float y;

    Vector2(float x, float y)
        : x(x)
        , y(y)
    {
    }

    float operator*(const Vector2& rhs) const {
        return x * rhs.x + y * rhs.y;
    }
};

class PerlinNoise {
public:
    enum class Interpolation {
        LINEAR,
        SMOOTH_STEP
    };

    PerlinNoise(int seed, int gridStep, int gridCellsWidth, int gridCellsHeight, Interpolation interpolation = Interpolation::SMOOTH_STEP);

    int width() const;
    int height() const;

    float getNoise(int x, int y);

    static float uniformize(float input);

private:
    float interpolate(float x, float y, float w);

    const Vector2& getGridGradient(int x, int y);

    const std::vector<Vector2> gradients_;
    const int gridStep_;
    const int gridCellsWidth_;
    const int gridCellsHeight_;
    const int width_;
    const int height_;
    const Interpolation interpolation_;
};
