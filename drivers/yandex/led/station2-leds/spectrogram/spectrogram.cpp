#include "spectrogram.h"

#include <algorithm>

YandexStation2Spectrogram::YandexStation2Spectrogram(size_t width, size_t height)
    : width_(width)
    , height_(height)
    , buffer_(width_ * height_)
{
}

float& YandexStation2Spectrogram::pixel(size_t x, size_t y) {
    static float oob;
    if (x >= width_ || y >= height_) {
        return oob;
    }
    return buffer_[y * width_ + x];
}

void YandexStation2Spectrogram::clear() {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
}
