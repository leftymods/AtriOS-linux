#pragma once

#include <yandex_io/protos/quasar_proto.pb.h>

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace quasar {

    struct EnumClassHash {
        template <typename T>
        std::size_t operator()(T t) const {
            return static_cast<std::size_t>(t);
        }
    };

    struct ScanResult {
    public:
        inline std::string to_string() const {
            return "[SSID=" + SSID + " BSSID=" + BSSID + " rssi=" + std::to_string(rssi) + " caps=" + capabilities + " rate=" + std::to_string(rate) + "]";
        }

        inline bool empty() const {
            return SSID.empty();
        }

        inline void reset()
        {
            SSID.clear();
            BSSID.clear();
            capabilities.clear();
            rssi = 0;
            rate = 0;
        }

        inline bool operator==(const struct ScanResult& b) const {
            return (SSID == b.SSID && BSSID == b.BSSID &&
                    capabilities == b.capabilities && rssi == b.rssi && rate == b.rate);
        }

        std::string SSID;
        std::string BSSID;
        std::string capabilities;
        int rssi;
        int rate;
    };

    struct WifiConfiguration {
    public:
        WifiConfiguration() {
            reset();
        }
        enum class KeyMgmt {
            NONE = 0,
            WPA_PSK,
            WPA_EAP,
            IEEE8021X,
            WPA2_PSK,
        };

        enum class GroupCipher {
            WEP40 = 0,
            WEP104,
            TKIP,
            CCMP,
            GTK_NOT_USED,
            SMS4
        };

        enum class AuthAlgorithm {
            OPEN = 0,
            SHARED,
            LEAP
        };

        enum class Protocol {
            WPA = 0,
            RSN,
            OSEN,
            WAPI
        };

        enum class PairwiseCipher {
            NONE = 0,
            TKIP,
            CCMP,
            SMS4
        };

        inline bool empty() const {
            return networkId == -1;
        };

        inline void reset()
        {
            frequency = -1;
            linkspeed = -1;
            status = 0;
            SSID.clear();
            BSSID.clear();
            networkId = -1;
            rssi = -9999;
            priority = -1;
            hiddenSSID = false;
            preSharedKey.clear();
            wepKeys.clear();
            wepTxKeyIndex = -1;
            allowedKeyManagement.clear();
            allowedGroupCiphers.clear();
            allowedProtocols.clear();
            allowedPairwiseCiphers.clear();
            allowedAuthAlgorithms.clear();
        };

        int frequency;
        int linkspeed;
        int status;
        int priority;
        std::string SSID;
        std::string BSSID;
        int networkId = -1;
        int rssi;
        bool hiddenSSID;
        std::string preSharedKey;
        std::unordered_map<int, std::string> wepKeys;
        int wepTxKeyIndex;
        std::vector<WifiConfiguration::KeyMgmt> allowedKeyManagement;
        std::vector<WifiConfiguration::GroupCipher> allowedGroupCiphers;
        std::vector<WifiConfiguration::Protocol> allowedProtocols;
        std::vector<WifiConfiguration::PairwiseCipher> allowedPairwiseCiphers;
        std::vector<WifiConfiguration::AuthAlgorithm> allowedAuthAlgorithms;
    };

    enum class DetailedState {
        CONNECTED = 0,
        CONNECTING,
        AUTHENTICATING,
        CAPTIVE_PORTAL_CHECK,
        OBTAINING_IPADDR,
        SCANNING,
        DISCONNECTED,
    };

    inline bool operator==(const quasar::proto::WifiInfo& lhs,
                           const quasar::proto::WifiInfo& rhs)
    {
        return (lhs.ssid() == rhs.ssid() && lhs.mac() == rhs.mac() &&
                lhs.secure() == rhs.secure() && lhs.rssi() == rhs.rssi());
    }

} // namespace quasar
