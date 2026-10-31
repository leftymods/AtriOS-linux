#pragma once

#include <memory>
#include <string>
#include <vector>

class SpectrumFilter {
public:
    virtual ~SpectrumFilter() = default;

    virtual void filter(std::vector<float>& bands) = 0;

    virtual void reset() = 0;
};

class Spectrogram {
public:
    Spectrogram() = default;

    virtual ~Spectrogram() = default;

    virtual std::string getExtraData() const {
        return "";
    }

    virtual size_t getBandCount() const = 0;

    virtual float getMinFrequency() const = 0;

    virtual float getMaxFrequency() const = 0;

    virtual void draw(const std::vector<float>& bands) = 0;

    virtual void clear() = 0;

    virtual void reset();

public:
    void update(std::vector<float>&& bands);

protected:
    void addFilter(std::unique_ptr<SpectrumFilter> filter);

    void resetFilters();

private:
    std::vector<std::unique_ptr<SpectrumFilter>> filters_;
};
