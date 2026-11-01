#include "wifi_utils.h"

#include "wifi_manager.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/protos/quasar_proto.pb.h>
#include <yandex_io/protos/enum_names/enum_names.h>

YIO_DEFINE_LOG_MODULE("wifi");

using namespace quasar;

const std::string& quasar::DetailedState_Name(const DetailedState& value)
{
    static std::string names[] = {
        "CONNECTED",
        "CONNECTING",
        "AUTHENTICATING",
        "CAPTIVE_PORTAL_CHECK",
        "OBTAINING_IPADDR",
        "SCANNING",
        "DISCONNECTED",
        "UNKNOWN",
    };

    switch (value)
    {
        case DetailedState::CONNECTED:
            return names[0];
        case DetailedState::CONNECTING:
            return names[1];
        case DetailedState::AUTHENTICATING:
            return names[2];
        case DetailedState::CAPTIVE_PORTAL_CHECK:
            return names[3];
        case DetailedState::OBTAINING_IPADDR:
            return names[4];
        case DetailedState::SCANNING:
            return names[5];
        case DetailedState::DISCONNECTED:
            return names[6];
        default:
            return names[7];
    }
}

int WifiUtils::startWifiConnect(const std::string& ssid, const std::string& password,
                                const proto::WifiType& type, bool hidden, WifiManager& wifiManager, bool reassociate)
{
    YIO_LOG_INFO("Connecting to wifi " << ssid << ", type: " << wifiTypeName(type));

    WifiConfiguration conf;
    if (ssid[0] != '"') {
        conf.SSID = "\"" + ssid + "\"";
    } else {
        conf.SSID = ssid;
    }
    conf.hiddenSSID = hidden;
    switch (type) {
        case proto::WifiType::OPEN:
            conf.allowedKeyManagement.push_back(WifiConfiguration::KeyMgmt::NONE);
            break;
        case proto::WifiType::WEP:
            conf.wepKeys.insert({0, "\"" + password + "\""});
            conf.wepTxKeyIndex = 0;
            conf.allowedKeyManagement.push_back(WifiConfiguration::KeyMgmt::NONE);
            conf.allowedGroupCiphers.push_back(WifiConfiguration::GroupCipher::WEP40);
            break;
        case proto::WifiType::WPA:
            conf.preSharedKey = "\"" + password + "\"";
            conf.allowedKeyManagement.push_back(WifiConfiguration::KeyMgmt::WPA_PSK);
            conf.allowedProtocols.push_back(WifiConfiguration::Protocol::RSN);
            conf.allowedProtocols.push_back(WifiConfiguration::Protocol::WPA);
            conf.allowedPairwiseCiphers.push_back(WifiConfiguration::PairwiseCipher::CCMP);
            conf.allowedPairwiseCiphers.push_back(WifiConfiguration::PairwiseCipher::TKIP);
            conf.allowedGroupCiphers.push_back(WifiConfiguration::GroupCipher::WEP40);
            conf.allowedGroupCiphers.push_back(WifiConfiguration::GroupCipher::WEP104);
            conf.allowedGroupCiphers.push_back(WifiConfiguration::GroupCipher::CCMP);
            conf.allowedGroupCiphers.push_back(WifiConfiguration::GroupCipher::TKIP);
            conf.allowedAuthAlgorithms.push_back(WifiConfiguration::AuthAlgorithm::OPEN);
            break;
        default:
            return -1;
    }
    conf.status = 2;
    conf.priority = 0;

    int netId = wifiManager.addNetwork(conf);
    if (netId == -1) {
        YIO_LOG_INFO("addNetwork returned -1");
    } else {
        YIO_LOG_INFO("networkId =  " << netId);
    }
    tryEnableWifi(wifiManager, netId, reassociate);

    return netId;
}

bool WifiUtils::tryEnableWifi(WifiManager& wifiManager, int netId, bool reassociate)
{
    if (netId != -1)
    {
        if (!wifiManager.enableNetwork(netId, true)) {
            YIO_LOG_ERROR_EVENT("WifiUtils.FailedEnableNetwork", "wifiManager.enableNetwork() failed");
            return false;
        }

        if (reassociate && !wifiManager.reassociate()) {
            YIO_LOG_ERROR_EVENT("WifiUtils.FailedReassociate", "wifiManager.reassociate() failed");
            return false;
        }

        YIO_LOG_INFO("Wifi state: " << DetailedState_Name(wifiManager.getWifiState()));
    }
    return true;
}

proto::WifiType WifiUtils::parseType(const std::string& capabilities)
{
    if (capabilities.empty()) {
        return proto::WifiType::UNKNOWN_WIFI_TYPE;
    }
    if (capabilities.find("[WEP") != std::string::npos) {
        return proto::WifiType::WEP;
    }
    if (capabilities.find("[WPA") != std::string::npos) {
        return proto::WifiType::WPA;
    }
    return proto::WifiType::OPEN;
}

const std::unordered_map<std::string, WifiConfiguration::KeyMgmt> WifiUtils::keyMgmtMap = {
    {"NONE", WifiConfiguration::KeyMgmt::NONE},
    {"WPA-PSK", WifiConfiguration::KeyMgmt::WPA_PSK},
    {"WPA-EAP", WifiConfiguration::KeyMgmt::WPA_EAP},
    {"IEEE8021X", WifiConfiguration::KeyMgmt::IEEE8021X},
    {"WPA2-PSK", WifiConfiguration::KeyMgmt::WPA2_PSK},
};

const std::unordered_map<WifiConfiguration::KeyMgmt, std::string, EnumClassHash> WifiUtils::keyMgmtStringMap = {
    {WifiConfiguration::KeyMgmt::NONE, "NONE"},
    {WifiConfiguration::KeyMgmt::WPA_PSK, "WPA-PSK"},
    {WifiConfiguration::KeyMgmt::WPA_EAP, "WPA-EAP"},
    {WifiConfiguration::KeyMgmt::IEEE8021X, "IEEE8021X"},
    {WifiConfiguration::KeyMgmt::WPA2_PSK, "WPA2-PSK"},
};

const std::unordered_map<std::string, WifiConfiguration::GroupCipher> WifiUtils::groupCipherMap = {
    {"WEP40", WifiConfiguration::GroupCipher::WEP40},
    {"WEP104", WifiConfiguration::GroupCipher::WEP104},
    {"TKIP", WifiConfiguration::GroupCipher::TKIP},
    {"CCMP", WifiConfiguration::GroupCipher::CCMP},
    {"GTK-NOT-USED", WifiConfiguration::GroupCipher::GTK_NOT_USED},
    {"SMS4", WifiConfiguration::GroupCipher::SMS4},
};

const std::unordered_map<WifiConfiguration::GroupCipher, std::string, EnumClassHash> WifiUtils::groupCipherStringMap = {
    {WifiConfiguration::GroupCipher::WEP40, "WEP40"},
    {WifiConfiguration::GroupCipher::WEP104, "WEP104"},
    {WifiConfiguration::GroupCipher::TKIP, "TKIP"},
    {WifiConfiguration::GroupCipher::CCMP, "CCMP"},
    {WifiConfiguration::GroupCipher::GTK_NOT_USED, "GTK-NOT-USED"},
    {WifiConfiguration::GroupCipher::SMS4, "SMS4"},
};

const std::unordered_map<std::string, WifiConfiguration::AuthAlgorithm> WifiUtils::authAlgorithmMap = {
    {"OPEN", WifiConfiguration::AuthAlgorithm::OPEN},
    {"SHARED", WifiConfiguration::AuthAlgorithm::SHARED},
    {"LEAP", WifiConfiguration::AuthAlgorithm::LEAP},
};

const std::unordered_map<WifiConfiguration::AuthAlgorithm, std::string, EnumClassHash> WifiUtils::authAlgorithmStringMap = {
    {WifiConfiguration::AuthAlgorithm::OPEN, "OPEN"},
    {WifiConfiguration::AuthAlgorithm::SHARED, "SHARED"},
    {WifiConfiguration::AuthAlgorithm::LEAP, "LEAP"},
};

const std::unordered_map<std::string, WifiConfiguration::Protocol> WifiUtils::protocolMap = {
    {"WPA", WifiConfiguration::Protocol::WPA},
    {"RSN", WifiConfiguration::Protocol::RSN},
    {"OSEN", WifiConfiguration::Protocol::OSEN},
    {"WAPI", WifiConfiguration::Protocol::WAPI},
};

const std::unordered_map<WifiConfiguration::Protocol, std::string, EnumClassHash> WifiUtils::protocolStringMap = {
    {WifiConfiguration::Protocol::WPA, "WPA"},
    {WifiConfiguration::Protocol::RSN, "RSN"},
    {WifiConfiguration::Protocol::OSEN, "OPEN"},
    {WifiConfiguration::Protocol::WAPI, "WAPI"},
};

const std::unordered_map<std::string, WifiConfiguration::PairwiseCipher> WifiUtils::pairwiseCipherMap = {
    {"NONE", WifiConfiguration::PairwiseCipher::NONE},
    {"TKIP", WifiConfiguration::PairwiseCipher::TKIP},
    {"CCMP", WifiConfiguration::PairwiseCipher::CCMP},
    {"SMS4", WifiConfiguration::PairwiseCipher::SMS4},
};

const std::unordered_map<WifiConfiguration::PairwiseCipher, std::string, EnumClassHash> WifiUtils::pairwiseCipherStringMap = {
    {WifiConfiguration::PairwiseCipher::NONE, "NONE"},
    {WifiConfiguration::PairwiseCipher::TKIP, "TKIP"},
    {WifiConfiguration::PairwiseCipher::CCMP, "CCMP"},
    {WifiConfiguration::PairwiseCipher::SMS4, "SMS4"},
};
