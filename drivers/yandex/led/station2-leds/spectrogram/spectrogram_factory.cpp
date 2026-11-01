#include "bricks_spectrogram.h"
#include "fluid_spectrogram.h"
#include "pads_spectrogram.h"
#include "random_spectrogram.h"
#include "spectrogram_factory.h"
#include "wave_spectrogram.h"

std::unique_ptr<YandexStation2Spectrogram> createSpectrogram(
    size_t width, size_t height,
    const Json::Value& config,
    const Json::Value& presets,
    const std::string& extraData)
{
    const std::string type = quasar::tryGetString(config, "type", "wave");

    if (type == "wave") {
        return std::make_unique<WaveSpectrogram>(width, height, config);
    } else if (type == "pads") {
        return std::make_unique<PadsSpectrogram>(width, height, config);
    } else if (type == "bricks") {
        return std::make_unique<BricksSpectrogram>(width, height, config);
    } else if (type == "fluid") {
        return std::make_unique<FluidSpectrogram>(width, height, config);
    } else if (type == "random") {
        return std::make_unique<RandomSpectrogram>(width, height, config, presets, extraData);
    }

    throw std::runtime_error("unknown spectrogram type");
}
