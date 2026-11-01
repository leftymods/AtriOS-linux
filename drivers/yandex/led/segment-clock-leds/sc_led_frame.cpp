#include "sc_led_frame.h"

#include <algorithm>
#include <map>
#include <sstream>

#include <iostream>

namespace {
    const uint32_t TIME_STRING_LENGTH = 5;                       // like "15:32"
    const uint32_t SCLED_STRING_LENGHT = TIME_STRING_LENGTH + 1; // time string + DEGREE symbol, like "15+65*"

    const std::vector<uint32_t> DIGIT_INDICIES = {0, 1, 3, 4};
    const uint32_t SYMBOL_INDEX = 2;
    const uint32_t DEGREE_INDEX = 5;

    const std::vector<std::string> DIGIT_POSITIONS = {"1", "2", "3", "4"};
    const std::vector<std::string> DIGIT_PARTS = {"A", "B", "C", "D", "E", "F", "G"};
    const std::string PLUS = "PLUS";
    const std::vector<std::string> PLUS_POSITIONS = {"1", "2"};
    const ScLedLocation MINUS_LOC = {"MINUS", ""};
    const ScLedLocation DEGREE_LOC = {"DEGREE", ""};

    const std::map<uint8_t, std::vector<std::string>> DIGIT_VALUES = {
        {' ', {}},
        {'0', {"A", "B", "C", "D", "E", "F"}},
        {'1', {"B", "C"}},
        {'2', {"A", "B", "D", "E", "G"}},
        {'3', {"A", "B", "C", "D", "G"}},
        {'4', {"B", "C", "F", "G"}},
        {'5', {"A", "C", "D", "F", "G"}},
        {'6', {"A", "C", "D", "E", "F", "G"}},
        {'7', {"A", "B", "C"}},
        {'8', {"A", "B", "C", "D", "E", "F", "G"}},
        {'9', {"A", "B", "C", "D", "F", "G"}},
        {'-', {"G"}}};

    const std::map<uint8_t, std::vector<ScLedLocation>> SYMBOL_VALUES = {
        {' ', {}},
        {':', {{"PLUS", "1"}, {"PLUS", "2"}}},
        {'-', {MINUS_LOC}},
        {'+', {{"PLUS", "1"}, {"PLUS", "2"}, MINUS_LOC}}};
} // namespace

ScLedFrame ScLedFrame::makeEmptyFrame(std::chrono::milliseconds delayMs)
{
    ScLedFrame frame;
    for (const auto& position : DIGIT_POSITIONS) {
        for (const auto& part : DIGIT_PARTS) {
            frame.elements[{part, position}] = brightnessOff;
        }
    }
    for (const auto& position : PLUS_POSITIONS) {
        frame.elements[{PLUS, position}] = brightnessOff;
    }
    frame.elements[MINUS_LOC] = brightnessOff;
    frame.elements[DEGREE_LOC] = brightnessOff;

    frame.delayMs = delayMs;

    return frame;
}

ScLedFrame ScLedFrame::makeFrameFromPatternString(std::string str)
{
    str.erase(std::remove(str.begin(), str.end(), '|'), str.end());
    std::istringstream stream(str);
    auto values = std::vector<uint32_t>(std::istream_iterator<uint32_t>(stream),
                                        std::istream_iterator<uint32_t>());

    if (values.size() != TOTAL_LEDS_COUNT + 1) {
        throw std::runtime_error("Wrong led format, got " + std::to_string(values.size()) + " str = " + str);
    }

    for (size_t i = 0; i < TOTAL_LEDS_COUNT; ++i) {
        if (values[i] < 0 || values[i] > 255) {
            throw std::runtime_error("Wrong brightness value, got " + std::to_string(values[i]));
        }
    }

    ScLedFrame frame;
    frame.delayMs = std::chrono::milliseconds(values[values.size() - 1]);

    uint32_t ind = 0;
    for (const auto& position : DIGIT_POSITIONS) {
        for (const auto& part : DIGIT_PARTS) {
            frame.elements[{part, position}] = values[ind];
            ind++;
        }
    }
    for (const auto& position : PLUS_POSITIONS) {
        frame.elements[{PLUS, position}] = values[ind];
        ind++;
    }
    frame.elements[MINUS_LOC] = values[ind];
    ind++;
    frame.elements[DEGREE_LOC] = values[ind];

    return frame;
}

ScLedFrame ScLedFrame::makeFrameFromValues(std::vector<uint8_t>::const_iterator begin,
                                           std::vector<uint8_t>::const_iterator end,
                                           std::chrono::milliseconds delayMs)
{
    ScLedFrame frame;
    frame.delayMs = delayMs;

    std::string str;

    if (end - begin != TOTAL_LEDS_COUNT) {
        throw std::runtime_error("Wrong led frame size, got " + std::to_string(begin - end));
    }

    auto it = begin;

    for (const auto& position : DIGIT_POSITIONS) {
        for (const auto& part : DIGIT_PARTS) {
            frame.elements[{part, position}] = *it;
            it++;
        }
    }
    for (const auto& position : PLUS_POSITIONS) {
        frame.elements[{PLUS, position}] = *it;
        it++;
    }
    frame.elements[MINUS_LOC] = *it;
    it++;
    frame.elements[DEGREE_LOC] = *it;

    return frame;
}

ScLedFrame ScLedFrame::makeFrameFromTimeString(const std::string& timeStr)
{
    if (timeStr.length() != TIME_STRING_LENGTH) {
        throw std::runtime_error("Wrong time string length. timeStr: " + timeStr);
    }

    return makeFrameFromString(timeStr + ' ');
}

ScLedFrame ScLedFrame::makeFrameFromString(const std::string& scStr, uint8_t brightness, std::chrono::milliseconds delayMs)
{
    if (scStr.length() != SCLED_STRING_LENGHT) {
        throw std::runtime_error("Wrong string length. scStr: " + scStr);
    }

    ScLedFrame frame = makeEmptyFrame();

    // handle 4 main led symbols
    for (auto i : DIGIT_INDICIES) {
        auto iter = DIGIT_VALUES.find(scStr[i]);
        if (iter == DIGIT_VALUES.end()) {
            throw std::runtime_error("Got unexpected digit symbol '" + std::to_string(scStr[i]) + "' in scStr: " + scStr);
        }

        std::string position;
        if (i == 0 || i == 1) {
            position = std::to_string(i + 1);
        } else {
            position = std::to_string(i);
        }

        for (const auto& part : iter->second) {
            frame.elements[{part, position}] = brightness;
        }
    }

    // handle middle symbol
    {
        auto iter = SYMBOL_VALUES.find(scStr[SYMBOL_INDEX]);
        if (iter == SYMBOL_VALUES.end()) {
            throw std::runtime_error("Got unexpected symbol '" + std::to_string(scStr[SYMBOL_INDEX]) + "' in scStr: " + scStr);
        }
        for (const auto& ledLoc : iter->second) {
            frame.elements[ledLoc] = brightness;
        }
    }

    // handle degree symbol
    if (scStr[DEGREE_INDEX] != ' ') {
        frame.elements[DEGREE_LOC] = brightness;
    }

    frame.delayMs = delayMs;

    return frame;
}
