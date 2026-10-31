#pragma once

#include <json/json.h>

#include <map>
#include <string>

struct FontConfig {
    std::string fontFileName;
    int paddingX;
    int paddingY;
};

namespace quasar {

    class ConfigUtils {
    public:
        static const std::string ANIMATIONS_CACHE_CONFIG;
        static const std::string DIRECTIVE_ANIMATION_CONFIG;
        static const std::string LED_DEVICES_CONFIG;
        static const std::string LED_USER_CONFIG;
        static const std::string MAIND_CONFIG;
        static const std::string RCU_CONFIG;
        static const std::string SPECTROGRAM_ANIMATION_CONFIG;
        static const std::string DEVICE_STATE_HANDLER_CONFIG;
        static const std::string DO_NOT_DISTURB_CONFIG;

        static FontConfig getClockFont(const Json::Value& config, const std::string& aliceFrontalAnimationFontsPath);

    private:
        static std::map<std::string, FontConfig> FONT_DEFAULTS;
    };

} // namespace quasar
