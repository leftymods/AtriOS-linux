#include "random_spectrogram.h"
#include "spectrogram_factory.h"

#include <yandex_io/libs/logging/logging.h>

RandomSpectrogram::RandomSpectrogram(
    size_t width, size_t height,
    const Json::Value& config,
    const Json::Value& presets,
    const std::string& extraData)
    : YandexStation2Spectrogram(width, height)
    , parameters_(loadParameters(config))
    , presets_(presets)
    , gen_(std::random_device()())
    , currentPick_(parameters_.presets.size() - 1)
{
    if (!extraData.empty()) {
        auto json = quasar::tryParseJsonOrEmpty(extraData);
        if (json.isMember("pick")) {
            auto forcedPickName = json["pick"];
            for (unsigned presetIndex = 0; presetIndex < parameters_.presets.size(); ++presetIndex) {
                if (forcedPickName == parameters_.presets[presetIndex]) {
                    forcedPick_ = static_cast<int>(presetIndex);
                    break;
                }
            }
        }
    }
    pickSpectrogram();
}

std::string RandomSpectrogram::getExtraData() const {
    return "{\"pick\":\"" + parameters_.presets[currentPick_] + "\"}";
}

size_t RandomSpectrogram::getBandCount() const {
    return spectrogram_->getBandCount();
}

float RandomSpectrogram::getMinFrequency() const {
    return spectrogram_->getMinFrequency();
}

float RandomSpectrogram::getMaxFrequency() const {
    return spectrogram_->getMaxFrequency();
}

void RandomSpectrogram::draw(const std::vector<float>& bands) {
    spectrogram_->update(std::vector<float>(bands));

    clear();

    for (size_t x = 0; x < getWidth(); x++) {
        for (size_t y = 0; y < getHeight(); y++) {
            pixel(x, y) = spectrogram_->pixel(x, y);
        }
    }

    if (parameters_.interval != std::chrono::seconds::zero() && std::chrono::steady_clock::now() >= nextSwitchTime_)
    {
        pickSpectrogram();
    }
}

void RandomSpectrogram::reset() {
    pickSpectrogram();
}

RandomSpectrogram::Parameters RandomSpectrogram::loadParameters(const Json::Value& config) {
    Parameters parameters;

    if (const auto& presets = config["presets"]) {
        parameters.presets.reserve(presets.size());
        for (const auto& p : presets) {
            parameters.presets.push_back(p.asString());
        }
    }

    if (const auto& interval = config["interval"]) {
        parameters.interval = std::chrono::seconds(interval.asUInt());
    }

    return parameters;
}

void RandomSpectrogram::pickSpectrogram() {
    if (parameters_.presets.empty()) {
        YIO_LOG_INFO("No presets, falling back to default spectrogram");

        spectrogram_ = createSpectrogram(
            getWidth(),
            getHeight(),
            Json::Value(),
            Json::Value(),
            "");

        return;
    }

    if (forcedPick_ >= 0) {
        currentPick_ = forcedPick_;
    } else {
        std::uniform_int_distribution<> distrib(0, parameters_.presets.size() - 2);
        int pick = distrib(gen_);
        if (pick >= currentPick_) {
            pick++;
        }
        currentPick_ = pick;
    }

    const auto& preset = parameters_.presets[currentPick_];

    YIO_LOG_INFO("Picked preset '" << preset << "'");

    const auto& config = presets_[preset];

    spectrogram_ = createSpectrogram(
        getWidth(),
        getHeight(),
        config,
        Json::Value(),
        "");

    YIO_LOG_INFO("Picked preset extra data:" << spectrogram_->getExtraData());

    nextSwitchTime_ = std::chrono::steady_clock::now() + parameters_.interval;
}
