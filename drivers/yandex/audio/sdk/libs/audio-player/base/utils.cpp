#include "utils.h"

#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>

namespace {

    bool isValidGstIdentifier(const std::string& s) {
        // https://a.yandex-team.ru/arc/trunk/arcadia/contrib/restricted/gstreamer/gst/parse/parse.l?rev=6435725#L54
        static const std::regex IDENTIFIER("[[:alnum:]_][[:alnum:]_%:-]*");
        return std::regex_match(s, IDENTIFIER);
    }

} // namespace

namespace quasar {

    std::string escapeGstString(std::string_view s) {
        // https://a.yandex-team.ru/arc/trunk/arcadia/contrib/restricted/gstreamer/gst/parse/parse.l?rev=6435725#L56-57
        std::string result;
        result += '"';
        for (char c : s) {
            if (c == '\0') {
                break; // ¯\_(ツ)_/¯
            }
            if (c == '"') {
                result += '\\';
            }
            result += c;
        }
        result += '"';
        return result;
    }

    std::string serializeGstOptions(const std::map<std::string, std::string>& options) {
        std::stringstream ss;
        for (const auto& [key, value] : options) {
            if (!isValidGstIdentifier(key)) {
                throw std::runtime_error("Bad gstreamer option name: " + key);
            }
            ss << key << "=" << escapeGstString(value) << " ";
        }
        return ss.str();
    }

    double calcNormalizationDb(const AudioPlayer::Params::Normalization& n) {
        constexpr double maxTruePeak = -1.0;
        const double delta = n.targetLufs - n.integratedLoudness;
        if (delta < 0.0) {
            return delta;
        }

        if (n.truePeak + delta <= maxTruePeak) {
            return delta;
        }
        return std::max(maxTruePeak - n.truePeak, 0.0);
    }

    double calcGstNormalizationVolume(const AudioPlayer::Params::Normalization& n) {
        // note: max value for gstreamer "volume" plugin is 10. so limit max volume
        const double db = std::min(calcNormalizationDb(n), 20.0);
        return pow(10.0, db / 20.0);
    }

} // namespace quasar
