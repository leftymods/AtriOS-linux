#pragma once

#include "wifi_types.h"

#include <yandex_io/protos/quasar_proto.pb.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace quasar {

    class WifiManager;

    const std::string& DetailedState_Name(const DetailedState& value);

    class WifiUtils {
    public:
        static int startWifiConnect(const std::string& ssid, const std::string& password, const quasar::proto::WifiType& type, bool hidden, quasar::WifiManager& wifiManager, bool reassociate);
        static quasar::proto::WifiType parseType(const std::string& capabilities);

        static const std::unordered_map<std::string, WifiConfiguration::KeyMgmt> keyMgmtMap;
        static const std::unordered_map<std::string, WifiConfiguration::GroupCipher> groupCipherMap;
        static const std::unordered_map<std::string, WifiConfiguration::AuthAlgorithm> authAlgorithmMap;
        static const std::unordered_map<std::string, WifiConfiguration::Protocol> protocolMap;
        static const std::unordered_map<std::string, WifiConfiguration::PairwiseCipher> pairwiseCipherMap;

        static const std::unordered_map<WifiConfiguration::KeyMgmt, std::string, EnumClassHash> keyMgmtStringMap;
        static const std::unordered_map<WifiConfiguration::GroupCipher, std::string, EnumClassHash> groupCipherStringMap;
        static const std::unordered_map<WifiConfiguration::AuthAlgorithm, std::string, EnumClassHash> authAlgorithmStringMap;
        static const std::unordered_map<WifiConfiguration::Protocol, std::string, EnumClassHash> protocolStringMap;
        static const std::unordered_map<WifiConfiguration::PairwiseCipher, std::string, EnumClassHash> pairwiseCipherStringMap;

    private:
        static bool tryEnableWifi(quasar::WifiManager& wifiManager, int netId, bool reassociate);
    };

} // namespace quasar
