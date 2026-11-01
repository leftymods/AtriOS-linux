#pragma once
#include "zigbee.h"

#include <optional>
#include <string>
#include <utility>

#include <util/stream/file.h>
#include <util/folder/path.h>

namespace zigbee {
    std::optional<zigbee::EUI64> eui64FromString(const std::string& s);

    std::optional<std::pair<zigbee::NetworkParameters, zigbee::proto::NetworkParameters::Version>> loadNetwork(const TFsPath& file);
} // namespace zigbee
