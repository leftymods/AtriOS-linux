#include "filters.h"

#include <deque>
#include <numeric>

namespace {

    template <class T>
    class MovingAverage {
    public:
        explicit MovingAverage(size_t n)
            : values_(n)
        {
            if (!n) {
                throw std::runtime_error("n must be positive");
            }
        }

        void update(T value) {
            values_.pop_front();
            values_.push_back(value);
            result_ = std::accumulate(values_.begin(), values_.end(), T()) / values_.size();
        }

        T get() const {
            return result_;
        }

        size_t size() const {
            return values_.size();
        }

    private:
        std::deque<T> values_;
        T result_;
    };

} // namespace

class SmoothingSpectrumFilter: public SpectrumFilter {
public:
    explicit SmoothingSpectrumFilter(float factor)
        : factor_(factor)
    {
    }

    void filter(std::vector<float>& bands) override {
        if (last_.empty()) {
            last_.resize(bands.size());
        }

        if (bands.size() != last_.size()) {
            throw std::runtime_error("unexpected band count");
        }

        for (size_t i = 0; i < bands.size(); i++) {
            bands[i] = last_[i] * factor_ + bands[i] * (1.0f - factor_);
            last_[i] = bands[i];
        }
    }

    void reset() override {
        last_.clear();
    }

private:
    const float factor_;

    std::vector<float> last_;
};

class NormalizingSpectrumFilter: public SpectrumFilter {
public:
    explicit NormalizingSpectrumFilter(size_t windowSize, float min = 1e-5f)
        : scale_(windowSize)
        , min_(min)
    {
    }

    void filter(std::vector<float>& bands) override {
        scale_.update(*std::max_element(bands.begin(), bands.end()));
        const float scale = std::max(min_, scale_.get());
        for (auto& v : bands) {
            v /= scale;
        }
    }

    void reset() override {
        scale_ = MovingAverage<float>(scale_.size());
    }

private:
    MovingAverage<float> scale_;
    const float min_;
};

std::unique_ptr<SpectrumFilter> createSmoothingSpectrumFilter(float factor) {
    return std::make_unique<SmoothingSpectrumFilter>(factor);
}

std::unique_ptr<SpectrumFilter> createNormalizingSpectrumFilter(size_t windowSize) {
    return std::make_unique<NormalizingSpectrumFilter>(windowSize);
}
