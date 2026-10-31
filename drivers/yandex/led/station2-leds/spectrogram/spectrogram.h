#pragma once

#include <smart_devices/libs/spectrogram/spectrogram.h>

#include <vector>

class YandexStation2Spectrogram: public Spectrogram {
public:
    YandexStation2Spectrogram(size_t width, size_t height);

    void clear() override;

public:
    inline size_t getWidth() const {
        return width_;
    }

    inline size_t getHeight() const {
        return height_;
    }

    float& pixel(size_t x, size_t y);

private:
    const size_t width_;
    const size_t height_;

    std::vector<float> buffer_;
};
