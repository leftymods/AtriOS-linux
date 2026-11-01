#include "sc_led_storage.h"
#include "sc_led_volume_animation.h"

#include <yandex_io/libs/logging/logging.h>
#include <util/generic/scope.h>
#include <util/system/yassert.h>

#include <dirent.h>
#include <memory>

YIO_DEFINE_LOG_MODULE("leds");

ScLedStorage::ScLedStorage(std::weak_ptr<ScLedController> ledController, const std::string& ledPatternsPath)
    : ledController_(std::move(ledController))
{
    Y_VERIFY(ledController_.lock() != nullptr);
    Y_VERIFY(!ledPatternsPath.empty());

    loadPatterns(ledPatternsPath);
}

std::shared_ptr<ScLedPattern> ScLedStorage::getPattern(const std::string& name) const {
    auto res = patterns_.find(name);
    if (res != patterns_.end()) {
        auto pattern = std::make_shared<ScLedPattern>(*res->second);
        pattern->resetAnimation();
        return pattern;
    } else {
        YIO_LOG_ERROR_EVENT("ScLedStorage.GetPattern.UnexpectedPattern", "Cannot find sc pattern " + name);
        throw std::runtime_error("Cannot find sc pattern " + name);
    }
}

std::shared_ptr<ScLedPattern> ScLedStorage::getIdlePattern() const {
    return ScLedPattern::getIdlePattern(ledController_);
}

std::shared_ptr<ScLedPattern> ScLedStorage::getVolumePattern(int currentVolume, int maxVolume) const {
    return std::make_shared<ScLedVolumeAnimation>(currentVolume, maxVolume, ledController_);
}

std::shared_ptr<ScLedClockAnimation> ScLedStorage::getClockAnimation(int64_t utcOffsetSeconds, bool withDegree, bool withSmoothLightUp) const {
    return std::make_shared<ScLedClockAnimation>(ledController_, utcOffsetSeconds, withDegree, withSmoothLightUp);
}

std::shared_ptr<ScLedFadingClockAnimation> ScLedStorage::getFadingClockAnimation(int64_t utcOffsetSeconds, bool withDegree) const {
    return std::make_shared<ScLedFadingClockAnimation>(ledController_, utcOffsetSeconds, withDegree);
}

std::shared_ptr<ScLedBlinkingClockAnimation> ScLedStorage::getBlinkingClockAnimation(int64_t utcOffsetSeconds, bool withDegree) const {
    return std::make_shared<ScLedBlinkingClockAnimation>(ledController_, utcOffsetSeconds, withDegree);
}

std::shared_ptr<ScLedTimerCountdownAnimation> ScLedStorage::getCountdownAnimation(
    std::chrono::system_clock::time_point timerEnd, bool withDegree, bool withSmoothLightUp) const {
    return std::make_shared<ScLedTimerCountdownAnimation>(ledController_, timerEnd, withDegree, withSmoothLightUp);
}

void ScLedStorage::loadPatterns(const std::string& pathToPatternsDirectory)
{
    DIR* dp = opendir(pathToPatternsDirectory.c_str());

    if (dp == nullptr) {
        YIO_LOG_ERROR_EVENT("ScLedStorage.LoadPatterns.OpendirFailed", "Cannot open directory " + pathToPatternsDirectory);
        throw std::runtime_error("Cannot open directory " + pathToPatternsDirectory);
    }

    Y_DEFER {
        closedir(dp);
    };

    dirent* ep = nullptr;
    while ((ep = readdir(dp)) != nullptr) {
        const char* file = ep->d_name;
        if (strstr(file, ".scled") && !strstr(file, ".scled~")) {
            std::string pathToPattern = pathToPatternsDirectory;
            if (pathToPattern.back() != '/') {
                pathToPattern += '/';
            }
            pathToPattern += file;
            try {
                patterns_[std::string(file)] = ScLedPattern::loadFromFile(pathToPattern, ledController_);
            } catch (const std::exception& e) {
                YIO_LOG_WARN("Cannot load sc pattern from file " << pathToPattern << ": " << e.what());
            }
        }
    }
}
