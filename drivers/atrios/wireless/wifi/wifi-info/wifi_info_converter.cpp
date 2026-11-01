#include "wifi_info_converter.h"

namespace {
    using namespace YandexIO;

    WifiInfo::WifiType convertWifiType(const quasar::proto::WifiType& protoWifiType) {
        switch (protoWifiType) {
            case quasar::proto::WifiType::WEP:
                return WifiInfo::WifiType::WEP;
            case quasar::proto::WifiType::WPA:
                return WifiInfo::WifiType::WPA;
            case quasar::proto::WifiType::OPEN:
                return WifiInfo::WifiType::OPEN;
            case quasar::proto::WifiType::UNKNOWN_WIFI_TYPE:
                return WifiInfo::WifiType::UNKNOWN_WIFI_TYPE;
            default:
                return WifiInfo::WifiType::NONE_WIFI_TYPE;
        }
    }

    quasar::proto::WifiType convertWifiType(const WifiInfo::WifiType& wifiType) {
        switch (wifiType) {
            case WifiInfo::WifiType::WEP:
                return quasar::proto::WifiType::WEP;
            case WifiInfo::WifiType::WPA:
                return quasar::proto::WifiType::WPA;
            case WifiInfo::WifiType::OPEN:
                return quasar::proto::WifiType::OPEN;
            case WifiInfo::WifiType::UNKNOWN_WIFI_TYPE:
                return quasar::proto::WifiType::UNKNOWN_WIFI_TYPE;
            default:
                return quasar::proto::WifiType::NONE_WIFI_TYPE;
        }
    }
} // namespace

namespace YandexIO {
    WifiInfo convertWifiInfoFromProto(const quasar::proto::WifiInfo& protoWifiInfo) {
        WifiInfo wifiInfo;
        wifiInfo.mac = protoWifiInfo.mac();
        wifiInfo.channelWidth = protoWifiInfo.channel();
        wifiInfo.freq = protoWifiInfo.freq();
        wifiInfo.rssi = protoWifiInfo.rssi();
        wifiInfo.ssid = protoWifiInfo.ssid();
        wifiInfo.wifiType = convertWifiType(protoWifiInfo.wifi_type());
        wifiInfo.isConnected = protoWifiInfo.is_connected();
        wifiInfo.isCorporate = protoWifiInfo.is_corporate();
        return wifiInfo;
    }

    quasar::proto::WifiInfo convertWifiInfoToProto(const WifiInfo& wifiInfo) {
        quasar::proto::WifiInfo protoWifiInfo;
        protoWifiInfo.set_mac(TString(wifiInfo.mac));
        protoWifiInfo.set_channel(wifiInfo.channelWidth);
        protoWifiInfo.set_freq(wifiInfo.freq);
        protoWifiInfo.set_rssi(wifiInfo.rssi);
        protoWifiInfo.set_ssid(TString(wifiInfo.ssid));
        protoWifiInfo.set_wifi_type(convertWifiType(wifiInfo.wifiType));
        protoWifiInfo.set_is_connected(wifiInfo.isConnected);
        protoWifiInfo.set_is_corporate(wifiInfo.isCorporate);
        return protoWifiInfo;
    }

    std::vector<WifiInfo> convertWifiListFromProto(const quasar::proto::WifiList& protoWifiList) {
        std::vector<YandexIO::WifiInfo> wifiList(protoWifiList.hotspots_size());
        for (auto i = 0; i < protoWifiList.hotspots_size(); ++i) {
            wifiList[i] = YandexIO::convertWifiInfoFromProto(protoWifiList.hotspots(i));
        }
        return wifiList;
    }

    quasar::proto::WifiList convertWifiListToProto(const std::vector<WifiInfo>& wifiList) {
        quasar::proto::WifiList protoWifiList;
        for (const auto& wifiInfo : wifiList) {
            protoWifiList.add_hotspots()->CopyFrom(YandexIO::convertWifiInfoToProto(wifiInfo));
        }
        return protoWifiList;
    }
} // namespace YandexIO
