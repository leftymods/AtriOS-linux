#include "led_color_config.h"
#include "utils.h"

using namespace quasar;

class LedColorConfig::Impl {
public:
    virtual ~Impl() = default;

    virtual std::tuple<float, float, float> convertColor(float r, float g, float b) = 0;
};

namespace {

    class NullImpl: public LedColorConfig::Impl {
    public:
        std::tuple<float, float, float> convertColor(float r, float g, float b) override {
            return std::make_tuple(r, g, b);
        }
    };

    class HueCurveImpl: public LedColorConfig::Impl {
    public:
        HueCurveImpl(const Json::Value& config) {
            if (const auto& curve = config["hueCurve"]) {
                hueCurve_.clear();

                for (const auto& point : curve) {
                    hueCurve_.emplace(point[0].asFloat(), point[1].asFloat());
                }
            }

            if (const auto& wp = config["whitePoint"]) {
                float wr = std::clamp(wp[0].asFloat(), 0.0f, 1.0f);
                float wg = std::clamp(wp[1].asFloat(), 0.0f, 1.0f);
                float wb = std::clamp(wp[2].asFloat(), 0.0f, 1.0f);

                whitePoint_ = std::make_tuple(wr, wg, wb);
            }
        }

        std::tuple<float, float, float> convertColor(float r, float g, float b) override {
            float h;
            float s;
            float v;
            std::tie(h, s, v) = rgbToHsv(r, g, b);

            auto u = hueCurve_.upper_bound(h);
            auto l = std::prev(u);

            float t = (h - l->first) / (u->first - l->first);
            h = l->second * (1.0f - t) + u->second * t;

            float rc;
            float gc;
            float bc;
            std::tie(rc, gc, bc) = hsvToRgb(h, s, v);

            float m = std::min(rc, std::min(gc, bc));

            rc -= (1.0f - std::get<0>(whitePoint_)) * m;
            gc -= (1.0f - std::get<1>(whitePoint_)) * m;
            bc -= (1.0f - std::get<2>(whitePoint_)) * m;

            return std::make_tuple(rc, gc, bc);
        }

    private:
        std::map<float, float> hueCurve_ = {
            {0.0f, 0.0f},
            {360.0f, 360.0f}};
        std::tuple<float, float, float> whitePoint_ = {1.0f, 1.0f, 1.0f};
    };

    class MatrixImpl: public LedColorConfig::Impl {
    public:
        MatrixImpl(const Json::Value& config) {
            if (const auto& matrix = config["matrix"]) {
                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 3; j++) {
                        matrix_[i][j] = matrix[i][j].asFloat();
                    }
                }
            }
        }

        std::tuple<float, float, float> convertColor(float r, float g, float b) override {
            float rc = matrix_[0][0] * r + matrix_[0][1] * g + matrix_[0][2] * b;
            float gc = matrix_[1][0] * r + matrix_[1][1] * g + matrix_[1][2] * b;
            float bc = matrix_[2][0] * r + matrix_[2][1] * g + matrix_[2][2] * b;

            float max = std::max(rc, std::max(gc, bc));
            if (max > 1.0f) {
                rc /= max;
                gc /= max;
                bc /= max;
            }

            return std::make_tuple(
                std::clamp(rc, 0.0f, 1.0f),
                std::clamp(gc, 0.0f, 1.0f),
                std::clamp(bc, 0.0f, 1.0f));
        }

    private:
        std::array<std::array<float, 3>, 3> matrix_ = {
            std::array<float, 3>{1.0f, 0.0f, 0.0f},
            std::array<float, 3>{0.0f, 1.0f, 0.0f},
            std::array<float, 3>{0.0f, 0.0f, 1.0f}};
    };

} // namespace

LedColorConfig::LedColorConfig(const Json::Value& config)
{
    const auto type = tryGetString(config, "type", "null");

    if (type == "null") {
        impl_ = std::make_unique<NullImpl>();
    } else if (type == "hueCurve") {
        impl_ = std::make_unique<HueCurveImpl>(config);
    } else if (type == "matrix") {
        impl_ = std::make_unique<MatrixImpl>(config);
    } else {
        throw std::runtime_error("Unknown LED color config type");
    }
}

LedColorConfig::~LedColorConfig() = default;

std::tuple<float, float, float> LedColorConfig::convertColor(float r, float g, float b) {
    return impl_->convertColor(r, g, b);
}
