#include "config_utils.h"

#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

using namespace quasar;

const std::string ConfigUtils::ANIMATIONS_CACHE_CONFIG = "animationsCache";
const std::string ConfigUtils::DIRECTIVE_ANIMATION_CONFIG = "directiveAnimation";
const std::string ConfigUtils::LED_DEVICES_CONFIG = "ledDevicesConfig";
const std::string ConfigUtils::LED_USER_CONFIG = "led";
const std::string ConfigUtils::MAIND_CONFIG = "maind";
const std::string ConfigUtils::RCU_CONFIG = "rcu";
const std::string ConfigUtils::SPECTROGRAM_ANIMATION_CONFIG = "spectrogramAnimation";
const std::string ConfigUtils::DEVICE_STATE_HANDLER_CONFIG = "deviceStateHandlerConfig";
const std::string ConfigUtils::DO_NOT_DISTURB_CONFIG = "dndMode";

std::map<std::string, FontConfig> ConfigUtils::FONT_DEFAULTS = {
    {"small", {"clock_small.gif", 4, 5}},
    {"middle", {"clock_middle.gif", 2, 4}},
    {"large", {"clock_large.gif", 0, 3}}};

FontConfig ConfigUtils::getClockFont(const Json::Value& config, const std::string& aliceFrontalAnimationFontsPath) {
    auto font = FONT_DEFAULTS["middle"];

    if (config.isMember("size")) {
        const auto size = config["size"].asString();
        if (!FONT_DEFAULTS.count(size)) {
            YIO_LOG_ERROR_EVENT("ConfigUtils.UnexpectedClockSize", "Unexpected clock size received: " << size);
            font = FONT_DEFAULTS["middle"];
        } else {
            font = FONT_DEFAULTS[size];
        }
    }

    YIO_LOG_INFO("Switch clock font to: " << font.fontFileName);
    return FontConfig{
        aliceFrontalAnimationFontsPath + font.fontFileName,
        font.paddingX, font.paddingY};
}
