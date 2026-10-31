#include "utils.h"

#include <yandex_io/libs/base/utils.h>

#include <smart_devices/libs/zigbee/proto/network.pb.h>

#include <util/string/hex.h>

namespace zigbee {
    std::optional<zigbee::EUI64> eui64FromString(const std::string& s) {
        if (s.length() != 16) {
            return std::nullopt;
        }

        try {
            zigbee::EUI64 eui64;
            HexDecode(s.data(), eui64.size() * 2, eui64.data());
            return eui64;
        } catch (const yexception&) {
            return std::nullopt;
        }
    }

    std::optional<std::pair<zigbee::NetworkParameters, zigbee::proto::NetworkParameters::Version>> loadNetwork(const TFsPath& file) {
        const TString networkData = quasar::getFileContent(file.GetPath());
        zigbee::proto::NetworkParameters proto;
        Y_PROTOBUF_SUPPRESS_NODISCARD proto.ParseFromString(networkData);

        return std::pair(zigbee::NetworkParameters::fromProto(proto), proto.version());
    }
} // namespace zigbee
