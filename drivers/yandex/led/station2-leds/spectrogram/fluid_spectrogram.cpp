#include <smart_devices/libs/spectrogram/filters.h>
#include "fluid_spectrogram.h"

#include <yandex_io/libs/logging/logging.h>

FluidSpectrogram::FluidSpectrogram(
    size_t width, size_t height,
    const Json::Value& config)
    : YandexStation2Spectrogram(width, height)
    , world_(b2Vec2(0.0f, -10.0f))
{
    (void)(config);

    // addFilter(createSmoothingSpectrumFilter(0.8f));
    addFilter(createNormalizingSpectrumFilter(10));

    float halfWidth = (float)width / 2;
    float halfHeight = (float)height / 2;

    {
        b2BodyDef bd;
        b2Body* ground = world_.CreateBody(&bd);

        b2ChainShape shape;
        const b2Vec2 vertices[4] = {
            b2Vec2(-halfWidth, -halfHeight),
            b2Vec2(-halfWidth, 3 * halfHeight),
            b2Vec2(halfWidth, 3 * halfHeight),
            b2Vec2(halfWidth, -halfHeight)};
        shape.CreateLoop(vertices, 4);
        ground->CreateFixture(&shape, 0.0f);
    }

    {
        const b2ParticleSystemDef particleSystemDef;
        particleSystem_ = world_.CreateParticleSystem(&particleSystemDef);
        particleSystem_->SetRadius(0.25f);

        b2ParticleGroupDef particleGroupDef;

        b2PolygonShape shape;
        shape.SetAsBox(halfWidth, halfHeight);

        particleGroupDef.shape = &shape;
        particleSystem_->CreateParticleGroup(particleGroupDef);
    }

    for (int i = 0; i < 100; i++) {
        step();
    }
}

size_t FluidSpectrogram::getBandCount() const {
    return 18;
}

float FluidSpectrogram::getMinFrequency() const {
    return 40.0f;
}

float FluidSpectrogram::getMaxFrequency() const {
    return 3000.0f;
}

void FluidSpectrogram::draw(const std::vector<float>& bands) {
    applyImpulses(bands);

    step();

    clear();
    drawParticles();
}

void FluidSpectrogram::applyImpulses(const std::vector<float>& bands) {
    for (int i = 0; i < particleSystem_->GetParticleCount(); i++) {
        const b2Vec2& pos = particleSystem_->GetPositionBuffer()[i];

        size_t band = (pos.x / getWidth() + 0.5f) * (bands.size() - 1);

        float impulse = std::clamp(bands[band], 0.0f, 1.0f) * 0.1f;

        if (std::isnan(impulse)) {
            continue;
        }

        particleSystem_->ParticleApplyLinearImpulse(i, b2Vec2{0.0f, impulse});
    }
}

void FluidSpectrogram::step() {
    world_.Step(0.04f, 8, 3);
}

void FluidSpectrogram::drawParticles() {
    for (int i = 0; i < particleSystem_->GetParticleCount(); i++) {
        const b2Vec2& pos = particleSystem_->GetPositionBuffer()[i];

        int x = (float)getWidth() / 2 + pos.x;
        int y = (float)getHeight() / 2 + pos.y;

        pixel(x, y) += 0.125f;
    }
}
