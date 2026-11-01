#pragma once

#include "spectrogram.h"

#include <yandex_io/libs/json_utils/json_utils.h>

std::unique_ptr<YandexStation2Spectrogram> createSpectrogram(
    size_t width, size_t height,
    const Json::Value& config,
    const Json::Value& presets,
    const std::string& extraData);
