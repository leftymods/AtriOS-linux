#include "wifi_info.h"

#include "netlink_base.h"
#include "netlink_monitor.h"

#include <linux/genetlink.h>
#include <linux/nl80211.h>
#include <linux/wireless.h> //for iw_statistics
#include <sys/ioctl.h>
#include <string.h>

#include <iostream>
#include <array>
#include <span>
#include <optional>
#include <iterator>

#include <json/json.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/configuration/configuration.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/protos/quasar_proto.pb.h>
#include <util/folder/path.h>

using namespace quasar::net;

namespace {
    unsigned channelFromFreq(unsigned freq) {
        if (freq == 2484) { // 802.11-2007 17.3.8.3.2 and Annex J
            return 14;
        }
        if (freq == 5935) { // 802.11ax D6.1 27.3.23.2 and Annex E
            return 2;
        }
        if (freq < 2484) {
            return (freq - 2407) / 5;
        }
        if (freq >= 4910 && freq <= 4980) {
            return (freq - 4000) / 5;
        }
        if (freq < 5950) {
            return (freq - 5000) / 5;
        }
        if (freq <= 45000) { // DMG band lower limit 802.11ax D6.1 27.3.23.2
            return (freq - 5950) / 5;
        }
        if (freq >= 58320 && freq <= 70200) {
            return (freq - 56160) / 2160;
        }
        return 0;
    }

    // nlattr helpers
    char* nlattrPayloadPtr(const struct nlattr* attr) {
        return (char*)attr + NLA_HDRLEN;
    }

    unsigned nlattrPayloadLen(const struct nlattr* attr) {
        return attr->nla_len - NLA_HDRLEN;
    }

    template <typename T_>
    T_* getNlattrPayloadPtr(const struct nlattr* attr) {
        if (sizeof(T_) > nlattrPayloadLen(attr)) {
            throw std::runtime_error("Trying to get too large value from nlattr");
        }
        return (T_*)nlattrPayloadPtr(attr);
    }

    template <typename T_>
    T_ getNlattrPayloadMemcpy(const struct nlattr* attr) {
        T_ result;
        memcpy(&result, getNlattrPayloadPtr<T_>(attr), sizeof(result));
        return result;
    }

    template <typename T_>
    T_ getNlattrPayload(const struct nlattr* attr) {
        return *getNlattrPayloadPtr<T_>(attr);
    }

    template <>
    std::uint64_t getNlattrPayload<std::uint64_t>(const struct nlattr* attr) {
        return getNlattrPayloadMemcpy<std::uint64_t>(attr);
    }

    template <>
    [[maybe_unused]] std::int64_t getNlattrPayload<std::int64_t>(const struct nlattr* attr) {
        return getNlattrPayloadMemcpy<std::int64_t>(attr);
    }

    template <typename T_, typename Result_>
    Result_ getNlattrPayloadCast(const struct nlattr* attr) {
        return Result_(getNlattrPayload<T_>(attr));
    }

    template <typename T_>
    Json::Value getNlattrPayloadJ(const struct nlattr* attr) {
        return getNlattrPayload<T_>(attr);
    }

    template <typename T_, typename Result_>
    Json::Value getNlattrPayloadCastJ(const struct nlattr* attr) {
        return getNlattrPayloadCast<T_, Result_>(attr);
    }

    nlattr* nextNlattr(const struct nlattr* attr) {
        return (struct nlattr*)((char*)attr + NLMSG_ALIGN(attr->nla_len));
    }

    void iterateNlattr(const struct nlattr* attr, int payloadSize, auto callback) {
        while (payloadSize >= 0) {
            // std::cerr << "nla_type = " << attr->nla_type << " nla_len = " << attr->nla_len << " left " << payloadSize << '\n';
            callback(attr);
            payloadSize -= NLMSG_ALIGN(attr->nla_len);
            if (payloadSize <= 0) {
                break;
            }
            attr = nextNlattr(attr);
        }
    }

    void iterateNestedNlattr(const struct nlattr* attr, auto callback) {
        iterateNlattr(getNlattrPayloadPtr<struct nlattr>(attr), attr->nla_len - NLA_HDRLEN, callback);
    }

    void iterateNlattr(const struct nlmsghdr* nlmsg_ptr, auto callback) {
        int payloadSize = NLMSG_PAYLOAD(nlmsg_ptr, GENL_HDRLEN);
        struct nlattr* attr = (struct nlattr*)((char*)NLMSG_DATA(nlmsg_ptr) + GENL_HDRLEN);
        iterateNlattr(attr, payloadSize, callback);
    }

    template <int N>
    const std::array<std::uint8_t, N>& getNlattrByteArray(const struct nlattr* attr) {
        if (N > attr->nla_len - sizeof(*attr)) {
            throw std::runtime_error("Trying to get too large value from nlattr");
        }
        using ResultType = std::array<std::uint8_t, N>;
        return *((ResultType*)nlattrPayloadPtr(attr));
    }

    std::string getNlattrPayloadString(const struct nlattr* attr) {
        auto strSize = attr->nla_len - sizeof(*attr);
        if (strSize > 0) {
            return std::string(nlattrPayloadPtr(attr), strSize); // ignore trailing zero byte
        }
        return {};
    }

    std::string chanWidthToStr(std::uint32_t w) {
        const char* names[] = {
            "non-HT 20",
            "20",
            "40",
            "80",
            "80+80",
            "160",
            "5",
            "10",
        };
        if (w < std::size(names)) {
            return names[w];
        }
        return {};
    }

    std::string bssScanWidthToStr(std::uint32_t w) {
        const char* names[] = {
            "20",
            "10",
            "5",
        };
        if (w < std::size(names)) {
            return names[w];
        }
        return {};
    }

    double mbmToDouble(auto mbm) {
        return mbm / 100.0;
    }

    void ratesToArray(const unsigned char* data, unsigned len, Json::Value& dst) {
        for (unsigned i = 0; i < len; ++i) {
            int r = data[i] & 0x7f;
            auto frac = 5 * (r & 1);
            if (frac) {
                dst.append(std::to_string(r / 2) + '.' + std::to_string(frac));
            } else {
                dst.append(std::to_string(r / 2));
            }
        }
    }

    void iterateIE(const char* dataIn, unsigned len, auto callback) {
        const unsigned char* data = (const unsigned char*)dataIn;
        while (len >= 2 && data[1] <= len - 2) {
            unsigned elementLen = data[1];
            callback(data[0], elementLen, data + 2);
            elementLen += 2;
            len -= elementLen;
            data += elementLen;
        }
    }

    Json::Value calcAverageAccessDelay(unsigned in) { // according 802.11-2012 8.4.2.41
        Json::Value result = Json::arrayValue;
        std::tuple<unsigned, unsigned> a{0, 0};
        if (in == 0) {
            a = {0, 8};
        } else if (in == 1) {
            a = {8, 16};
        } else if (in <= 14) {
            a = {8 * in, 8 * in + 8};
        } else if (in == 15) {
            a = {120, 128};
        } else if (in == 16) {
            a = {128, 144};
        } else if (in <= 106) {
            a = {in * 16 - 128, (in + 1) * 16 - 128};
        } else if (in == 107) {
            a = {1584, 1600};
        } else if (in == 108) {
            a = {1600, 1632};
        } else {
            a = {in * 32 - 1856, (in + 1) * 32 - 1856};
        }
        auto [from, to] = a;
        result.append(from);
        result.append(to);
        return result;
    }

    Json::Value getIE(const struct nlattr* attr) {
        const char* data = nlattrPayloadPtr(attr);
        const unsigned len = nlattrPayloadLen(attr);
        Json::Value result;
        Json::Value rates{Json::arrayValue};
        iterateIE(data, len, [&result, &rates](unsigned type, unsigned len, const unsigned char* data) {
            YIO_LOG_DEBUG("IE[" << type << "] " << len << ' ' << rawDataToString(data, len));
            switch (type) {
                case 0:
                    result["SSID"] = std::string((char*)data, len);
                    break;
                case 1:
                    ratesToArray(data, len, rates);
                    break;
                case 7:
                    if (len >= 3) {
                        // collect 3 simbols if third one is not space
                        result["country"] = std::string((char*)data, data[2] > 0x20 ? 3 : 2);
                    }
                    break;
                case 11: {
                    if (len > 1) {
                        result["associated_stations"] = ((unsigned(data[1]) << 8) | data[0]);
                    }
                    if (len > 2) {
                        result["channel_utilization"] = (unsigned)data[2];
                    }
                    break;
                }
                case 50: // extended rates
                    ratesToArray(data, len, rates);
                    break;
                case 63: {
                    if (len >= 1) {
                        result["average_access_delay_usec"] = calcAverageAccessDelay(data[0]);
                    }
                    break;
                }
            }
        });
        if (!rates.empty()) {
            result["rates"] = std::move(rates);
        }
        return result;
    }

    std::string heGuardIntervalToFloat(std::uint8_t gi) {
        switch (gi) {
            case NL80211_RATE_INFO_HE_GI_1_6:
                return "1.6";
            case NL80211_RATE_INFO_HE_GI_3_2:
                return "3.2";
            default:
            case NL80211_RATE_INFO_HE_GI_0_8:
                break;
        }
        return "0.8";
    }

    using AttrToJsonFunc = Json::Value(const struct nlattr*);

    using CollectDescriptor = std::tuple<int, const char*, AttrToJsonFunc*>;

    Json::Value collectAttrByTbl(const struct nlattr* attr, auto& tbl) {
        Json::Value result = Json::objectValue;
        iterateNestedNlattr(attr, [&tbl, &result](const struct nlattr* attr) {
            auto pt = std::find_if(std::begin(tbl), std::end(tbl), [attr](auto desc) {
                return std::get<0>(desc) == attr->nla_type;
            });
            if (pt != std::end(tbl)) {
                result[std::get<1>(*pt)] = std::get<2>(*pt)(attr);
            }
        });
        return result;
    }

    Json::Value getNlattrTrueFlag(const struct nlattr* /*attr*/) {
        return true;
    }

    Json::Value collectHeGuardInterval(const struct nlattr* attr) {
        return heGuardIntervalToFloat(getNlattrPayload<std::uint8_t>(attr));
    }

    template <typename T_>
    Json::Value getBitrate(const struct nlattr* attr) { // value in 100kbit/s converting to Mbit/s
        auto val = getNlattrPayload<T_>(attr);
        return double(val / 10.0);
    }

    Json::Value collectBitrate(const struct nlattr* attr) {
        CollectDescriptor tbl[] = {
            {NL80211_RATE_INFO_BITRATE, "mbits_16", getBitrate<std::uint16_t>},
            {NL80211_RATE_INFO_SHORT_GI, "short_guard", getNlattrTrueFlag},
            {NL80211_RATE_INFO_BITRATE32, "mbits", getBitrate<std::uint32_t>},
            {NL80211_RATE_INFO_VHT_MCS, "VHT_MCS", getNlattrPayloadCastJ<std::uint8_t, int>},
            {NL80211_RATE_INFO_VHT_NSS, "VHT_NSS", getNlattrPayloadCastJ<std::uint8_t, int>},
            {NL80211_RATE_INFO_MCS, "MCS", getNlattrPayloadCastJ<std::uint8_t, int>},
            {NL80211_RATE_INFO_HE_MCS, "HE_MCS", getNlattrPayloadCastJ<std::uint8_t, int>},
            {NL80211_RATE_INFO_HE_NSS, "HE_NSS", getNlattrPayloadCastJ<std::uint8_t, int>},
            {NL80211_RATE_INFO_HE_GI, "he_guard_interval", collectHeGuardInterval},
            {NL80211_RATE_INFO_HE_DCM, "HE_DCM", getNlattrPayloadCastJ<std::uint8_t, int>},
        };
        auto result = collectAttrByTbl(attr, tbl);
        if (result.isMember("mbits")) {
            result.removeMember("mbits_16");
        }
        return result;
    }

    std::string capabilitiesToStr(std::uint16_t f, bool useLong) {
        static const char* longNames[] = {
            "ESS",
            "IBSS",
            "CF_POLLABLE",
            "CF_POLL_REQUEST",
            "PRIVACY",
            "SHORT_PREAMBLE",
            "PBCC",
            "CHANNEL_AGILITY",
            "SPECTRUM_MGMT",
            "QOS",
            "SHORT_SLOT_TIME",
            "APSD",
            "RADIO_MEASURE",
            "DSSS_OFDM",
            "DEL_BACK",
            "IMM_BACK",
        };
        static const char* shortNames[] = {
            "ESS",
            "IBSS",
            "CFPOLL",
            "CFPLREQ",
            "PRIV",
            "SHRTPRE",
            "PBCC",
            "CHAN_AG",
            "SPCTRMGM",
            "QOS",
            "SHRTSLT",
            "APSD",
            "RDMEASR",
            "D3SOFDM",
            "DELBCK",
            "IMMBCK",
        };
        std::string result;
        for (auto name : (useLong ? longNames : shortNames)) {
            if (f == 0) {
                break;
            }
            if (f & 1) {
                if (!result.empty()) {
                    result += ' ';
                }
                result += name;
            }
            f >>= 1;
        }
        return result;
    }

    std::string staFlagsToStr(std::uint32_t f) {
        // these flags are enumerated from 1, STA_FLAG_AUTHORIZED = 1
        const std::tuple<int, const char*> dmp[] = {
            {NL80211_STA_FLAG_AUTHORIZED, "AUTHORIZED"},
            {NL80211_STA_FLAG_SHORT_PREAMBLE, "SHORT_PREAMBLE"},
            {NL80211_STA_FLAG_WME, "WME"},
            {NL80211_STA_FLAG_MFP, "MFP"},
            {NL80211_STA_FLAG_AUTHENTICATED, "AUTHENTICATED"},
            {NL80211_STA_FLAG_TDLS_PEER, "TDLS_PEER"},
            {NL80211_STA_FLAG_ASSOCIATED, "ASSOC"},
        };
        std::string result;
        for (auto [flag, str] : dmp) {
            if (f & (1 << flag)) {
                if (!result.empty()) {
                    result += ' ';
                }
                result += str;
            }
        }
        return result;
    }

    enum class SignalMode {
        RCPI,
        DBM,
        MWDBM
    };

    double signal8toDouble(std::uint8_t signal, SignalMode mode) {
        switch (mode) {
            case SignalMode::RCPI:
                return double(signal / 2.0) - 110.0;
            case SignalMode::DBM:
                return signal > 63 ? signal - 0x100 : signal;
            default:
                break;
        }
        return 10.0 * log10(signal);
    }

    double signal8toDouble(const struct nlattr* attr, SignalMode mode) {
        return signal8toDouble(getNlattrPayload<std::uint8_t>(attr), mode);
    }

    Json::Value collectChainSignal(const struct nlattr* attr) {
        Json::Value result = Json::arrayValue;
        iterateNestedNlattr(attr, [&result](const struct nlattr* attr) {
            result.append(signal8toDouble(attr, SignalMode::DBM));
        });
        return result;
    }

    Json::Value collectTidStats(const struct nlattr* attr) {
        Json::Value result = Json::objectValue;
        iterateNestedNlattr(attr, [&result](const struct nlattr* attr) {
            std::uint64_t tx = 0;
            std::uint64_t tx_failed = 0;
            iterateNestedNlattr(attr, [&tx, &tx_failed](const struct nlattr* attr) {
                switch (attr->nla_type) {
                    case NL80211_TID_STATS_TX_MSDU:
                        tx = getNlattrPayload<std::uint64_t>(attr);
                        break;
                    case NL80211_TID_STATS_TX_MSDU_FAILED:
                        tx_failed = getNlattrPayload<std::uint64_t>(attr);
                        break;
                }
            });
            if (tx || tx_failed) {
                Json::Value tid = Json::objectValue;
                tid["tx"] = tx;
                tid["tx_failed"] = tx_failed;
                result[std::to_string(attr->nla_type)] = std::move(tid);
            }
        });
        return result;
    }

    std::tuple<std::string, std::uint16_t> processGenId(const struct nlmsghdr* nlmsg_ptr) {
        const struct genlmsghdr* genhdr = (struct genlmsghdr*)NLMSG_DATA(nlmsg_ptr);
        if (genhdr->cmd != CTRL_CMD_NEWFAMILY) {
            YIO_LOG_WARN("Unexpected cmd! Expect " << CTRL_CMD_NEWFAMILY << " got " << (int)genhdr->cmd);
        }
        std::string name;
        std::uint16_t id;
        iterateNlattr(nlmsg_ptr, [&name, &id](const struct nlattr* attr) {
            if (attr->nla_type == CTRL_ATTR_FAMILY_ID) {
                id = getNlattrPayload<std::uint16_t>(attr);
                YIO_LOG_DEBUG("gen id is " << id);
            } else if (attr->nla_type == CTRL_ATTR_FAMILY_NAME) {
                name = getNlattrPayloadString(attr);
                YIO_LOG_DEBUG("gen name is " << name);
            }
        });
        return {std::move(name), id};
    }

    class GetWifiInfo: public quasar::net::NetlinkBase {
        std::uint16_t nlId{0};
        std::uint32_t phyId{0};
        using MacAddress = std::array<std::uint8_t, 6>;
        MacAddress wifiMac = {};
        MacAddress wifiBssid = {};
        std::string wifiSsid;
        std::uint32_t wifiIdx;
        const WifiInfoSettings settings;
        Json::Value otherBsses{Json::arrayValue};
        Json::Value survey{Json::arrayValue};

        void notMyBSS(Json::Value val) {
            if (settings.allBss) {
                otherBsses.append(std::move(val));
            }
        }

    public:
        Json::Value result;

        GetWifiInfo(int idx, const WifiInfoSettings& cfg)
            : NetlinkBase(NETLINK_GENERIC, 0)
            , wifiIdx(static_cast<std::uint32_t>(idx))
            , settings(cfg)
        {
        }

        void collect() {
            std::vector<HandledRequest> requests = {
                {[this]() { getWifiLinkDetails(); }, [this](auto msg) { processGetWifiDetails(msg); }},
                {[this]() { getSurvey(); }, [this](auto msg) { processSurvey(msg); }},
                {[this]() { getWifiScan(); }, [this](auto msg) { processWifiScan(msg); }},
                {[this]() { getWifiLinkInfo(); }, [this](auto msg) { processGetWifiInterface(msg); }},
                {[this]() { getNl80211IdRequest(); }, [this](auto msg) {
                    const auto& [name, id] = processGenId(msg);
                    nlId = id;
                    YIO_LOG_DEBUG("nlId = " << nlId); }},
            };
            monitorImpl(false, std::move(requests));
            if (settings.allBss && !otherBsses.empty()) {
                result["other_bss"] = std::move(otherBsses);
            }
            if (!survey.empty()) {
                // add noise etc into bss
                if (result.isMember("bss")) {
                    auto& bss = result["bss"];
                    if (bss.isMember("freq")) {
                        const auto& freq = bss["freq"];
                        for (const auto& item : survey) {
                            if (item.isMember("freq") && item["freq"] == freq) {
                                auto itemCopy = item;
                                itemCopy.removeMember("freq");
                                itemCopy.removeMember("chan");
                                bss["survey"] = std::move(itemCopy);
                                break;
                            }
                        }
                    }
                }
                if (settings.survey) {
                    result["survey"] = std::move(survey);
                }
            }
        }

        struct GenericReq {
            struct nlmsghdr hdr;
            struct genlmsghdr gen;
            char buf[256];

            struct nlattr* firstNlattr() const {
                return (struct nlattr*)((char*)NLMSG_DATA(this) + GENL_HDRLEN);
            }

            template <typename T_>
            nlattr* addBytesParam(struct nlattr* attr, int type, const T_& value) {
                attr->nla_type = type;
                attr->nla_len = value.size() + NLA_HDRLEN;
                memcpy(nlattrPayloadPtr(attr), value.data(), value.size());
                hdr.nlmsg_len += NLMSG_ALIGN(attr->nla_len);
                return attr;
            }

            template <typename T_>
            nlattr* addBytesParam(int type, const T_& value) {
                return addBytesParam<T_>(firstNlattr(), type, value);
            }

            nlattr* addStringParam(int type, const char* value) {
                std::string_view str(value, strlen(value) + 1);
                return addBytesParam(type, str);
            }

            template <typename T_>
            nlattr* addParam(int type, const T_& value) {
                const std::span<std::uint8_t, sizeof(T_)> byteArray((std::uint8_t*)&value, sizeof(T_));
                return addBytesParam(type, byteArray);
            }
        };

        void getNl80211IdRequest() {
            GenericReq req;
            setupHeader(req, GENL_ID_CTRL);
            req.gen.cmd = CTRL_CMD_GETFAMILY;
            req.gen.version = 0x1;
            req.addStringParam(CTRL_ATTR_FAMILY_NAME, "nl80211");
            sendRequest(req);
        }

        void processGetWifiInterface(const struct nlmsghdr* nlmsg_ptr) {
            const struct genlmsghdr* genhdr = (struct genlmsghdr*)NLMSG_DATA(nlmsg_ptr);
            if (genhdr->cmd != NL80211_CMD_NEW_INTERFACE) {
                YIO_LOG_WARN("Unexpected cmd! Expect " << NL80211_CMD_NEW_INTERFACE << " got " << (int)genhdr->cmd);
            }
            Json::Value iface;
            iterateNlattr(nlmsg_ptr, [this, &iface](const struct nlattr* attr) {
                switch (attr->nla_type) {
                    case NL80211_ATTR_IFINDEX:
                    case NL80211_ATTR_IFNAME:
                    case NL80211_ATTR_IFTYPE:
                        // not interesting attributes. Don't needed in default loginfo below
                        break;
                    case NL80211_ATTR_WIPHY:
                        phyId = getNlattrPayload<std::int32_t>(attr);
                        YIO_LOG_DEBUG("phyid = " << phyId);
                        break;
                    case NL80211_ATTR_MAC:
                        wifiMac = getNlattrByteArray<6>(attr);
                        iface["mac"] = macToStr(wifiMac);
                        break;
                    case NL80211_ATTR_GENERATION: // snapshot number
                        iface["gen"] = getNlattrPayload<std::uint32_t>(attr);
                        break;
                    case NL80211_ATTR_WIPHY_FREQ: {
                        const auto freq = getNlattrPayload<std::uint32_t>(attr);
                        iface["freq"] = freq;
                        if (!iface.isMember("channel")) {
                            iface["channel"] = channelFromFreq(freq); // calcing from center freq is more valid
                        }
                        break;
                    }
                    case NL80211_ATTR_WIPHY_CHANNEL_TYPE: // deprecated
                        break;
                    case NL80211_ATTR_SSID:
                        wifiSsid = getNlattrPayloadString(attr);
                        iface["ssid"] = wifiSsid;
                        break;
                    case NL80211_ATTR_4ADDR:
                        break;
                    case NL80211_ATTR_WIPHY_TX_POWER_LEVEL: {
                        iface["pwr"] = mbmToDouble(getNlattrPayload<std::int32_t>(attr));
                        break;
                    }
                    case NL80211_ATTR_WDEV:
                        break;
                    case NL80211_ATTR_CHANNEL_WIDTH:
                        iface["chan_width"] = chanWidthToStr(getNlattrPayload<std::uint32_t>(attr));
                        break;
                    case NL80211_ATTR_CENTER_FREQ1: {
                        const auto freq = getNlattrPayload<std::uint32_t>(attr);
                        iface["center_freq_1"] = freq;
                        iface["channel"] = channelFromFreq(freq);
                        break;
                    }
                    case NL80211_ATTR_TXQ_STATS:
                        /*
                        std::cout << "txq stats\n";
                        std::array<std::uint32_t, NL80211_TXQ_STATS_MAX + 1> txqStats = {};
                        iterateNestedNlattr(attr, [this, &txqStats](const struct nlattr* attr) {
                            if (attr->nla_type < txqStats.size()) {
                                txqStats[attr->nla_type] = getNlattrPayload<std::uint32_t>(attr);
                            }
                        });
                        for(auto counter: txqStats) {
                            std::cout << " " << counter << std::endl;
                        }
                        */
                        break;
                    default:
                        YIO_LOG_INFO("Unhandled GET_INTERFACE attribute " << attr->nla_type);
                }
            });
            if (settings.iface) {
                result["interface"] = std::move(iface);
            }
        }

        void getWifiLinkInfo() {
            YIO_LOG_DEBUG("GET_INTERFACE");
            GenericReq req;
            setupHeader(req, nlId);
            req.gen.cmd = NL80211_CMD_GET_INTERFACE;
            req.gen.version = 0x1;
            req.addParam(NL80211_ATTR_IFINDEX, wifiIdx);
            sendRequest(req);
        }

        std::tuple<Json::Value, bool> processBSS(const struct nlattr* attr) {
            Json::Value bss = Json::objectValue;
            Json::Value beacon = Json::objectValue;
            MacAddress bssid = {};
            bool used = false;
            bool ie_from_probe = false;
            iterateNestedNlattr(attr, [&](const struct nlattr* attr) {
                switch (attr->nla_type) {
                    case NL80211_BSS_BSSID:
                        bssid = getNlattrByteArray<6>(attr);
                        bss["bssid"] = macToStr(bssid);
                        break;
                    case NL80211_BSS_FREQUENCY: {
                        const auto freq = getNlattrPayload<std::uint32_t>(attr);
                        bss["freq"] = freq;
                        if (!bss.isMember("channel")) {
                            bss["channel"] = channelFromFreq(freq);
                        }
                        break;
                    }
                    case NL80211_BSS_TSF:
                        bss["TSF"] = getNlattrPayload<std::uint64_t>(attr);
                        break;
                    case NL80211_BSS_BEACON_INTERVAL:
                        beacon["interval"] = getNlattrPayloadCast<std::uint16_t, int>(attr);
                        break;
                    case NL80211_BSS_CAPABILITY: {
                        auto capa = getNlattrPayload<std::uint16_t>(attr);
                        bss["raw_cpblt"] = capa;
                        bss["cpblt"] = capabilitiesToStr(capa, settings.longCpblt);
                        break;
                    }
                    case NL80211_BSS_INFORMATION_ELEMENTS: // is probe if different with IES
                        bss["probe_ie"] = getIE(attr);
                        break;
                    case NL80211_BSS_SIGNAL_MBM: {
                        bss["signal_probe"] = mbmToDouble(getNlattrPayload<std::int32_t>(attr));
                        break;
                    }
                    case NL80211_BSS_SIGNAL_UNSPEC:
                        bss["signal_unspec"] = getNlattrPayload<std::uint8_t>(attr);
                        break;
                    case NL80211_BSS_STATUS:
                        if (getNlattrPayload<std::uint32_t>(attr)) {
                            used = true;
                        }
                        break;
                    case NL80211_BSS_SEEN_MS_AGO:
                        bss["seen_ago_ms"] = getNlattrPayload<std::uint32_t>(attr);
                        break;
                    case NL80211_BSS_BEACON_IES: {
                        beacon["ie"] = getIE(attr);
                        break;
                    }
                    case NL80211_BSS_CHAN_WIDTH:
                        bss["chan_width"] = bssScanWidthToStr(getNlattrPayload<std::uint32_t>(attr));
                        break;
                    case NL80211_BSS_PRESP_DATA:
                        ie_from_probe = true;
                        break;
                    case NL80211_BSS_BEACON_TSF:
                        beacon["last_tsf"] = getNlattrPayload<std::uint64_t>(attr);
                        break;
                    case NL80211_BSS_PARENT_BSSID:
                        bss["parent_bssid"] = macToStr(getNlattrByteArray<6>(attr));
                        break;
                    default:
                        YIO_LOG_INFO("Unhandled BSS attribute " << attr->nla_type);
                }
            });
            if (beacon.isMember("ie") && (!ie_from_probe || !settings.bothIE)) {
                bss.removeMember("probe_ie");
            }
            if (!beacon.empty()) {
                bss["beacon"] = std::move(beacon);
            }
            if (used) {
                wifiBssid = bssid;
            }
            return {bss, used};
        }

        void processWifiScan(const struct nlmsghdr* nlmsg_ptr) {
            iterateNlattr(nlmsg_ptr, [&, this](const struct nlattr* attr) {
                switch (attr->nla_type) {
                    case NL80211_ATTR_SSID:
                        result["SSID"] = getNlattrPayloadString(attr);
                        break;
                    case NL80211_ATTR_GENERATION:
                        result["gen"] = getNlattrPayload<std::uint32_t>(attr);
                        break;
                    case NL80211_ATTR_BSS: {
                        auto [bss, used] = processBSS(attr);
                        if (used) {
                            if (result.isMember("bss")) {
                                notMyBSS(std::move(bss));
                            } else {
                                result["bss"] = std::move(bss);
                            }
                        } else {
                            notMyBSS(std::move(bss));
                        }
                        break;
                    }
                }
            });
        }

        void getWifiScan() {
            YIO_LOG_DEBUG("GET_SCAN");
            GenericReq req;
            setupHeader(req, nlId);
            req.hdr.nlmsg_flags |= NLM_F_DUMP;
            req.gen.cmd = NL80211_CMD_GET_SCAN;
            req.gen.version = 1;
            req.addParam(NL80211_ATTR_IFINDEX, wifiIdx);
            sendRequest(req);
        }

        void collectStationInfo(const struct nlattr* attr) {
            Json::Value beacon = Json::objectValue;
            Json::Value rx = Json::objectValue;
            Json::Value tx = Json::objectValue;
            Json::Value signal = Json::objectValue;
            iterateNestedNlattr(attr, [this, &beacon, &rx, &tx, &signal](const struct nlattr* attr) {
                switch (attr->nla_type) {
                    case NL80211_STA_INFO_INACTIVE_TIME:
                        result["inact_ms"] = getNlattrPayload<std::uint32_t>(attr);
                        break;
                    case NL80211_STA_INFO_RX_BYTES64:
                        rx["bytes"] = getNlattrPayload<std::uint64_t>(attr);
                        break;
                    case NL80211_STA_INFO_TX_BYTES64:
                        tx["bytes"] = getNlattrPayload<std::uint64_t>(attr);
                        break;
                    case NL80211_STA_INFO_TX_BYTES:
                        if (!tx.isMember("bytes")) {
                            tx["bytes"] = getNlattrPayload<std::uint32_t>(attr);
                        }
                        break;
                    case NL80211_STA_INFO_RX_BYTES:
                        if (!rx.isMember("bytes")) {
                            rx["bytes"] = getNlattrPayload<std::uint32_t>(attr);
                        }
                        break;
                    case NL80211_STA_INFO_SIGNAL:
                        signal["dbm"] = signal8toDouble(attr, SignalMode::DBM);
                        break;
                    case NL80211_STA_INFO_SIGNAL_AVG:
                        signal["avg"] = signal8toDouble(attr, SignalMode::DBM);
                        break;
                    case NL80211_STA_INFO_RX_PACKETS:
                        rx["pkts"] = getNlattrPayload<std::uint32_t>(attr);
                        break;
                    case NL80211_STA_INFO_TX_PACKETS:
                        tx["pkts"] = getNlattrPayload<std::uint32_t>(attr);
                        break;
                    case NL80211_STA_INFO_TX_RETRIES:
                        tx["retries"] = getNlattrPayload<std::uint32_t>(attr);
                        break;
                    case NL80211_STA_INFO_TX_FAILED:
                        tx["fails"] = getNlattrPayload<std::uint32_t>(attr);
                        break;
                    case NL80211_STA_INFO_TX_BITRATE:
                        tx["rate"] = collectBitrate(attr);
                        break;
                    case NL80211_STA_INFO_RX_BITRATE:
                        rx["rate"] = collectBitrate(attr);
                        break;
                    case NL80211_STA_INFO_BSS_PARAM:
                        iterateNestedNlattr(attr, [this, &beacon](const struct nlattr* attr) {
                            switch (attr->nla_type) {
                                case NL80211_STA_BSS_PARAM_SHORT_SLOT_TIME:
                                    result["bss_short_slot_time"] = true;
                                    break;
                                case NL80211_STA_BSS_PARAM_DTIM_PERIOD:
                                    result["bss_dtim_period"] = getNlattrPayloadCast<std::uint8_t, unsigned>(attr);
                                    break;
                                case NL80211_STA_BSS_PARAM_BEACON_INTERVAL:
                                    beacon["interval"] = getNlattrPayload<std::uint16_t>(attr);
                                    break;
                            }
                        });
                        break;
                    case NL80211_STA_INFO_CONNECTED_TIME:
                        result["connected_time"] = getNlattrPayload<std::uint32_t>(attr);
                        break;
                    case NL80211_STA_INFO_STA_FLAGS: {
                        auto flags = getNlattrPayload<struct nl80211_sta_flag_update>(attr).set;
                        result["raw_flags"] = flags;
                        result["flags"] = staFlagsToStr(flags);
                        break;
                    }
                    case NL80211_STA_INFO_BEACON_LOSS:
                        beacon["loss"] = getNlattrPayload<std::uint32_t>(attr);
                        break;
                    case NL80211_STA_INFO_CHAIN_SIGNAL:
                        signal["chain"] = collectChainSignal(attr);
                        break;
                    case NL80211_STA_INFO_CHAIN_SIGNAL_AVG:
                        signal["chain_avg"] = collectChainSignal(attr);
                        break;
                    case NL80211_STA_INFO_RX_DROP_MISC:
                        rx["drop"] = getNlattrPayload<std::uint64_t>(attr);
                        break;
                    case NL80211_STA_INFO_BEACON_RX:
                        beacon["rx"] = getNlattrPayload<std::uint64_t>(attr);
                        break;
                    case NL80211_STA_INFO_BEACON_SIGNAL_AVG:
                        beacon["signal_avg"] = signal8toDouble(attr, SignalMode::DBM);
                        break;
                    case NL80211_STA_INFO_TID_STATS:
                        if (settings.tids) {
                            result["TID"] = collectTidStats(attr);
                        }
                        break;
                    case NL80211_STA_INFO_EXPECTED_THROUGHPUT:
                        result["expected_trhoghput_kbps"] = getNlattrPayload<std::uint32_t>(attr);
                        break;
                    case NL80211_STA_INFO_TX_DURATION:
                        tx["duration_usec"] = getNlattrPayload<std::uint64_t>(attr);
                        break;
                    case NL80211_STA_INFO_RX_DURATION:
                        rx["duration_usec"] = getNlattrPayload<std::uint64_t>(attr);
                        break;
                    case NL80211_STA_INFO_FCS_ERROR_COUNT:
                        result["fcs_error_count"] = getNlattrPayload<std::uint32_t>(attr);
                        break;
                    case NL80211_STA_INFO_ASSOC_AT_BOOTTIME:
                        result["boottime_assoc"] = getNlattrPayload<std::uint64_t>(attr);
                        break;
                    default:
                        YIO_LOG_INFO("Unhandled STA_INFO attribute " << attr->nla_type);
                }
            });
            if (!beacon.empty()) {
                result["beacon"] = std::move(beacon);
            }
            if (!rx.empty()) {
                result["rx"] = std::move(rx);
            }
            if (!tx.empty()) {
                result["tx"] = std::move(tx);
            }
            if (!signal.empty()) {
                result["signal"] = std::move(signal);
            }
        }

        void processGetWifiDetails(const struct nlmsghdr* nlmsg_ptr) {
            iterateNlattr(nlmsg_ptr, [this](const struct nlattr* attr) {
                if (attr->nla_type == NL80211_ATTR_STA_INFO) {
                    collectStationInfo(attr);
                } else if (attr->nla_type == NL80211_ATTR_MAC) {
                    YIO_LOG_DEBUG("Station mac " << macToStr(getNlattrByteArray<6>(attr)));
                } else if (attr->nla_type == NL80211_ATTR_GENERATION) {
                    result["stinfo_gen"] = getNlattrPayload<std::uint32_t>(attr);
                } else if (attr->nla_type == NL80211_ATTR_SCAN_SSIDS) {
                    YIO_LOG_DEBUG("ATTR SCAN SSIDS");
                } else if (attr->nla_type == NL80211_ATTR_IFINDEX) {
                    YIO_LOG_DEBUG("Interface index from station attribute = " << getNlattrPayload<std::uint32_t>(attr));
                } else {
                    YIO_LOG_INFO("Unhandled station attribute " << attr->nla_type);
                }
            });
        }

        void getWifiLinkDetails() {
            YIO_LOG_INFO("Requesting station");
            GenericReq req;
            setupHeader(req, nlId);
            req.gen.cmd = NL80211_CMD_GET_STATION;
            req.gen.version = 0x1;
            struct nlattr* attr = req.addParam(NL80211_ATTR_IFINDEX, wifiIdx);

            req.addBytesParam(nextNlattr(attr), NL80211_ATTR_MAC, wifiBssid);
            sendRequest(req);
        }

        void getSurvey() {
            YIO_LOG_INFO("Requesting survey " << wifiIdx);
            GenericReq req;
            setupHeader(req, nlId);
            req.hdr.nlmsg_flags |= NLM_F_DUMP;
            req.gen.cmd = NL80211_CMD_GET_SURVEY;
            req.gen.version = 1;
            req.addParam(NL80211_ATTR_IFINDEX, wifiIdx);
            sendRequest(req);
        }

        void processSurvey(const struct nlmsghdr* nlmsg_ptr) {
            YIO_LOG_DEBUG("Survey process " << nlmsg_ptr);
            iterateNlattr(nlmsg_ptr, [this](const struct nlattr* attr) {
                if (attr->nla_type == NL80211_ATTR_IFINDEX) {
                    YIO_LOG_DEBUG("survey ifindex " << getNlattrPayload<std::uint32_t>(attr));
                } else if (attr->nla_type == NL80211_ATTR_SURVEY_INFO) {
                    Json::Value item = Json::objectValue;
                    iterateNestedNlattr(attr, [&item](const struct nlattr* attr) {
                        switch (attr->nla_type) {
                            case NL80211_SURVEY_INFO_FREQUENCY: {
                                auto freq = getNlattrPayload<std::uint32_t>(attr);
                                item["freq"] = freq;
                                item["chan"] = channelFromFreq(freq);
                                break;
                            }
                            case NL80211_SURVEY_INFO_NOISE:
                                item["noise"] = signal8toDouble(attr, SignalMode::DBM);
                                break;
                            case NL80211_SURVEY_INFO_TIME:
                                item["ms"] = getNlattrPayload<std::uint64_t>(attr);
                                break;
                            case NL80211_SURVEY_INFO_TIME_BUSY:
                                item["busy"] = getNlattrPayload<std::uint64_t>(attr);
                                break;
                            default:
                                YIO_LOG_INFO("Unhandled survey_info attribute " << attr->nla_type << " " << attr->nla_len);
                        }
                    });
                    if (!item.empty()) {
                        survey.append(std::move(item));
                    }
                } else {
                    YIO_LOG_INFO("Unhandled survey attribute " << attr->nla_type << " " << attr->nla_len);
                }
            });
        }
    };

    struct DummyHandler: quasar::net::NetlinkMonitor::Handler {
        using Mon = quasar::net::NetlinkMonitor;
        using Scope = Mon::Scope;
        using IfFlags = Mon::IfFlags;
        void onAddress(IfIndex /*idx*/, std::string /*addr*/, Family /*family*/, Scope /*scope*/, bool /*local*/) override{};
        void onAddressRemove(IfIndex /*idx*/, std::string /*addr*/, Family /*family*/, Scope /*scope*/, bool /*local*/) override{};
        void onLink(IfIndex /*idx*/, std::string /*addr*/, IfFlags /*flags*/) override{};
        void onLinkRemoved(IfIndex /*idx*/) override{};
        void onMac(IfIndex /*idx*/, std::string /*mac*/) override{};
        void onStats(IfIndex /*idx*/, const Stats& /*status*/) override{};
    };

    struct GetDevIdHandler: public DummyHandler {
        const std::string& devName;
        std::optional<int> devIdx;

        GetDevIdHandler(const std::string& dev)
            : devName(dev)
        {
        }

        void onLink(IfIndex idx, std::string name, IfFlags /*flags*/) override {
            if (name == devName) {
                devIdx = idx;
            }
        }
    };

    int getNetDevIndex(const std::string& name) {
        GetDevIdHandler hndl(name);
        auto mon = makeNetlinkMonitor(hndl);

        mon->monitor(false);
        if (!hndl.devIdx.has_value()) {
            throw std::runtime_error("network device " + name + " does not exists");
        }
        return hndl.devIdx.value();
    };

    bool getWirelessStats(const std::string& devName, struct iw_statistics& dst) {
        FdHolder sock(socket(AF_INET, SOCK_DGRAM, 0));
        if (sock.fd == -1) {
            return false;
        }
        struct iwreq req = {
            .u = {
                .data = {
                    .pointer = &dst,
                    .length = sizeof(dst),
                }}};
        strncpy(req.ifr_name, devName.data(), std::size(req.ifr_name));
        memset(&dst, 0, sizeof(dst));
        if (ioctl(sock.fd, SIOCGIWSTATS, &req) == -1) {
            return false;
        }
        return true;
    }
} // namespace

namespace quasar::net {
    Json::Value getDetailedWifiInfo(const std::string& devName, const WifiInfoSettings& settings) {
        int idx = getNetDevIndex(devName);
        GetWifiInfo info(idx, settings);
        info.collect();
        struct iw_statistics iwStats;
        if (getWirelessStats(devName, iwStats)) {
            if (iwStats.qual.updated & (IW_QUAL_DBM | IW_QUAL_RCPI)) {
                const auto& q = iwStats.qual;
                auto& dst = info.result["iwstats"];
                dst = Json::objectValue;

                SignalMode mode = SignalMode::MWDBM;
                if (q.updated & IW_QUAL_RCPI) {
                    mode = SignalMode::RCPI;
                } else if (q.updated & IW_QUAL_DBM) {
                    mode = SignalMode::DBM;
                }

                dst["quality"] = (int)q.qual;
                if (!(q.updated & IW_QUAL_LEVEL_INVALID)) {
                    dst["level"] = signal8toDouble(q.level, mode);
                }
                if (!(q.updated & IW_QUAL_NOISE_INVALID)) {
                    dst["noise"] = signal8toDouble(q.noise, mode);
                }
            };
        }
        return info.result;
    }

    WifiInfo getWifiInfo(const std::shared_ptr<YandexIO::IDevice>& device) {
        const auto firstRunConfig = device->configuration()->getServiceConfig("firstrund");
        const auto wifiStoragePath = quasar::getString(firstRunConfig, "wifiStoragePath");

        // FIXME: very bad workaround. We touch wifiStoragePath file, which is also touched by FirstRunEndpoint.
        //  May be touched at the same time, but it's not very likely to happen, so let it be for some time.
        //  How must be fixed: get wifi status from another service (e.g. wifid, when it'll keep password or networkd etc.).
        if (!TFsPath(wifiStoragePath).Exists()) {
            throw std::runtime_error("No wifi info");
        }
        std::string serialized = quasar::getFileContent(wifiStoragePath);

        quasar::proto::WifiConnect currentWifi;
        if (!currentWifi.ParseFromString(TString(serialized))) {
            throw std::runtime_error(std::string("Cannot parse saved wifi network from ") + wifiStoragePath);
        }
        if (!currentWifi.has_wifi_id() || !currentWifi.has_password()) {
            throw std::runtime_error("Parsed wifi doesn't have required fields");
        }

        return WifiInfo{
            .ssid = currentWifi.wifi_id(),
            .password = currentWifi.password()};
    }

} // namespace quasar::net
