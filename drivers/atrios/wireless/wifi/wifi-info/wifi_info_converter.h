#pragma once

#include <yandex_io/sdk/wifi_info.h>

#include <yandex_io/protos/model_objects.pb.h>

namespace YandexIO {
    WifiInfo convertWifiInfoFromProto(const quasar::proto::WifiInfo& protoWifiInfo);
    quasar::proto::WifiInfo convertWifiInfoToProto(const WifiInfo& wifiInfo);

    std::vector<WifiInfo> convertWifiListFromProto(const quasar::proto::WifiList& protoWifiList);
    quasar::proto::WifiList convertWifiListToProto(const std::vector<WifiInfo>& wifiList);
} // namespace YandexIO
