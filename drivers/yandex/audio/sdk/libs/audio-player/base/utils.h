#pragma once

#include "audio_player.h"

#include <map>
#include <string>

namespace quasar {

    std::string escapeGstString(std::string_view s);

    std::string serializeGstOptions(const std::map<std::string, std::string>& options);

    double calcGstNormalizationVolume(const AudioPlayer::Params::Normalization& n);
    double calcNormalizationDb(const AudioPlayer::Params::Normalization& n);

} // namespace quasar
