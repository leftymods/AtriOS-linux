#include "audio_reader_config.h"

#include <yandex_io/libs/audio/common/defines.h>

#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/json_utils/json_utils.h>

using namespace quasar;

AudioReaderConfig::AudioReaderConfig(const Json::Value& config, const std::string& deviceType)
    : deviceName{tryGetString(config, "deviceName", "")}
    , cardNumber{tryGetInt(config, "cardNumber", DEFAULT_AUDIO_CARD_NUMBER)}
    , deviceNumber{tryGetInt(config, "deviceNumber", DEFAULT_AUDIO_DEVICE_NUMBER)}
    , inRate{tryGetInt(config, "inRate", DEFAULT_AUDIO_SAMPLING_RATE)}
    , micChannels{tryGetInt(config, "micChannels", DEFAULT_MIC_CHANNELS)}
    , spkChannels{tryGetInt(config, "spkChannels", DEFAULT_SPK_CHANNELS)}
    , beamformingChannels{tryGetInt(config, "beamformingChannels", DEFAULT_BEAMFORMING_CHANNELS)}
    , periodSize{tryGetInt(config, "periodSize", DEFAULT_AUDIO_PERIOD_SIZE)}
    , periodCount{tryGetInt(config, "periodCount", DEFAULT_AUDIO_PERIOD_COUNT)}
    , sampleSize{tryGetInt(config, "sampleSize", DEFAULT_AUDIO_SAMPLE_SIZE)}
    , vqeTypeName{tryGetString(config, "VQEtype", "")}
    , omniMode{tryGetBool(config, "omniMode", false)}
    , preset{tryGetString(config, "preset", deviceType)}
{
}
