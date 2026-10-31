#pragma once

#include "spectrogram.h"

std::unique_ptr<SpectrumFilter> createSmoothingSpectrumFilter(float factor);

std::unique_ptr<SpectrumFilter> createNormalizingSpectrumFilter(size_t windowSize);
