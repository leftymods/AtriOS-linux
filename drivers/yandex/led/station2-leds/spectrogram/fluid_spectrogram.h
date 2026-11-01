#pragma once

#include "spectrogram.h"

#include <contrib/libs/liquidfun/Box2D/Box2D.h>

#include <yandex_io/libs/json_utils/json_utils.h>

class FluidSpectrogram: public YandexStation2Spectrogram {
public:
    FluidSpectrogram(
        size_t width, size_t height,
        const Json::Value& config);

    size_t getBandCount() const override;

    float getMinFrequency() const override;

    float getMaxFrequency() const override;

    void draw(const std::vector<float>& bands) override;

private:
    void applyImpulses(const std::vector<float>& bands);

    void step();

    void drawParticles();

private:
    b2World world_;
    b2ParticleSystem* particleSystem_;
};
