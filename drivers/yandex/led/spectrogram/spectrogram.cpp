#include "spectrogram.h"

void Spectrogram::addFilter(std::unique_ptr<SpectrumFilter> filter) {
    filters_.push_back(std::move(filter));
}

void Spectrogram::update(std::vector<float>&& bands) {
    for (const auto& filter : filters_) {
        filter->filter(bands);
    }

    draw(bands);
}

void Spectrogram::resetFilters() {
    for (const auto& filter : filters_) {
        filter->reset();
    }
}

void Spectrogram::reset() {
    resetFilters();
}
