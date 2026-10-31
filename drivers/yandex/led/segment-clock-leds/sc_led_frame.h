#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

struct ScLedLocation {
    /* Part of the LED
     * Can take the following values:
     * A, B, C, D, E, F, G, PLUS, MINUS, DEGREE
     */
    std::string part;

    /* Position of the LED
     * For MINUS, DEGREE should be empty. Possible values for other parts:
     * 1, 2, 3, 4
     */
    std::string position;

    bool operator<(const ScLedLocation& rhs) const {
        if (this->part == rhs.part) {
            return this->position < rhs.position;
        }
        return this->part < rhs.part;
    }

    bool operator==(const ScLedLocation& rhs) const {
        return this->part == rhs.part && this->position == rhs.position;
    }
};

struct ScLedFrame {
    static constexpr uint32_t TOTAL_LEDS_COUNT = 32;
    static constexpr uint8_t brightnessOn = 255;
    static constexpr uint8_t brightnessOff = 0;

    static ScLedFrame makeEmptyFrame(std::chrono::milliseconds delayMs = std::chrono::milliseconds(0));
    static ScLedFrame makeFrameFromPatternString(std::string str);
    static ScLedFrame makeFrameFromValues(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end,
                                          std::chrono::milliseconds delayMs = std::chrono::milliseconds(0));
    static ScLedFrame makeFrameFromTimeString(const std::string& timeStr);
    static ScLedFrame makeFrameFromString(const std::string& scStr, uint8_t brightness = brightnessOn, std::chrono::milliseconds delayMs = std::chrono::milliseconds(0));

    std::map<ScLedLocation, uint8_t> elements;
    std::chrono::milliseconds delayMs{0};
};
