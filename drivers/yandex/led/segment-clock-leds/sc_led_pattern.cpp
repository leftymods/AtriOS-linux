#include "sc_led_pattern.h"

#include <util/system/yassert.h>

#include <yandex_io/libs/base/utils.h>

#include <algorithm>
#include <fstream>
#include <locale>
#include <vector>

ScLedPattern::ScLedPattern(std::string name, std::weak_ptr<ScLedController> ledController)
    : Animation(ledController)
    , name_(std::move(name))
    , ledDevice_(std::move(ledController))
{
}

std::shared_ptr<ScLedPattern> ScLedPattern::getIdlePattern(std::weak_ptr<ScLedController> ledController)
{
    auto res = std::make_shared<ScLedPattern>("idle.scled", std::move(ledController));
    res->frames_.push_back(ScLedFrame::makeEmptyFrame(std::chrono::seconds(300000)));
    res->loopFromIndex_ = 0;

    return res;
}

std::shared_ptr<ScLedPattern> ScLedPattern::loadFromFile(const std::string& fileName, std::weak_ptr<ScLedController> ledControllerWeak)
{
    std::ifstream patternFile(fileName);
    auto pattern = std::make_shared<ScLedPattern>(fileName, std::move(ledControllerWeak));

    if (patternFile.is_open()) {
        try {
            pattern->load(patternFile);
        } catch (const std::exception& e) {
            throw std::runtime_error("Cannot load LED pattern from file '" + fileName + "': " + e.what());
        }
    } else {
        throw std::runtime_error("Cannot open LED pattern file '" + fileName + "'");
    }

    return pattern;
}

void ScLedPattern::load(std::istream& stream)
{
    std::string line;

    while (std::getline(stream, line))
    {
        line = quasar::trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        if (line.rfind("loop", 0) == 0) {
            loopFromIndex_ = int(frames_.size());
            continue;
        }

        auto ledFrame = ScLedFrame::makeFrameFromPatternString(std::move(line));

        frames_.push_back(ledFrame);
    }
}

std::shared_ptr<ScLedPattern> ScLedPattern::loadFromBinary(const std::string& name,
                                                           const std::vector<uint8_t>& bytes,
                                                           std::weak_ptr<ScLedController> ledControllerWeak)
{
    auto pattern = std::make_shared<ScLedPattern>(name, std::move(ledControllerWeak));

    uint64_t ind = 0;
    while (ind < bytes.size()) {
        if (bytes[ind] == 1) { // loop
            pattern->loopFromIndex_ = int(pattern->frames_.size());
            ind++;
            continue;
        }

        if (bytes[ind] != 0) {
            throw std::runtime_error("Unexpected key byte at " + std::to_string(ind) + ". Expected 0 or 1, got " + std::to_string(bytes[ind]));
        }
        ind++;

        const auto firstByte = bytes.begin() + ind;
        const auto lastByte = bytes.begin() + ind + ScLedFrame::TOTAL_LEDS_COUNT;
        ind += ScLedFrame::TOTAL_LEDS_COUNT;

        uint16_t duration = bytes[ind] << 8 | bytes[ind + 1];
        auto durationMs = std::chrono::milliseconds(duration);
        ind += 2;

        auto ledFrame = ScLedFrame::makeFrameFromValues(firstByte, lastByte, durationMs);

        pattern->frames_.push_back(ledFrame);
    }

    return pattern;
}

void ScLedPattern::drawCurrentFrame()
{
    if (!started_) {
        return;
    }
    auto frame = getCurrentFrame();
    if (auto ledController = ledDevice_.lock()) {
        ledController->drawFrame(std::move(frame));
    }
}

const ScLedFrame& ScLedPattern::getCurrentFrame() const {
    Y_VERIFY(currentFrame_ < (int)frames_.size());
    return frames_[currentFrame_];
}

bool ScLedPattern::finished() const {
    return (currentFrame_ >= (int)frames_.size()) || (std::chrono::steady_clock::now() >= endOfDuration_);
}

ScLedPattern::TimePoint ScLedPattern::getEndOfFrameTimePoint() const {
    return std::min(endOfFrame_, endOfDuration_);
}

void ScLedPattern::resetAnimation() {
    started_ = false;
    currentFrame_ = 0;
}

void ScLedPattern::updateTime(Animation::TimePoint timePoint) {
    if (!started_) {
        return;
    }
    auto delta = timePoint - startOfCurrentFrame_;
    while (!finished() && (delta >= getCurrentFrame().delayMs)) {
        auto previousFrameDelay = getCurrentFrame().delayMs;
        startOfCurrentFrame_ += previousFrameDelay;
        delta -= previousFrameDelay;
        currentFrame_ += 1;
        if (!finished()) {
            endOfFrame_ = startOfCurrentFrame_ + getCurrentFrame().delayMs;
        }
    }

    if (currentFrame_ >= (int)frames_.size() && loopFromIndex_ >= 0) {
        currentFrame_ = loopFromIndex_;
        startOfCurrentFrame_ = timePoint;
        endOfFrame_ = startOfCurrentFrame_ + getCurrentFrame().delayMs;
    }
}

void ScLedPattern::startAnimationFrom(Animation::TimePoint startTime) {
    if (started_) {
        return;
    }
    started_ = true;
    startTime_ = startTime;
    startOfCurrentFrame_ = startTime;
    currentFrame_ = 0;
    //endOfDuration is handled separately
    if (!finished()) {
        endOfFrame_ = startTime + getCurrentFrame().delayMs;
    }
}

std::chrono::nanoseconds ScLedPattern::getLength() const {
    auto length = std::chrono::nanoseconds(0);
    for (const auto& frame : frames_) {
        length += std::chrono::milliseconds(frame.delayMs);
    }
    return length;
}

std::string ScLedPattern::getName() const {
    return name_;
}
