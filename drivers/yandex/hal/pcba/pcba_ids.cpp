#include "pcba_ids.h"
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/base/utils.h>

#include <util/system/yassert.h>

namespace quasar {

    std::optional<PCBAIds> loadPCBAIds(const std::string& path) {
        if (!fileExists(path)) {
            YIO_LOG_WARN("PcbaIds node doesn't exist: " << path);
            return std::nullopt;
        }
        const auto content = getFileContent(path);
        const auto optContentJson = tryParseJson(content);
        if (!optContentJson.has_value()) {
            YIO_LOG_WARN("Invalid pcba ids content: " << content);
            return std::nullopt;
        }
        const auto& contentJson = *optContentJson;
        if (!contentJson.isArray()) {
            YIO_LOG_WARN("Invalid pcba ids content format (not array): " << content);
            return std::nullopt;
        }
        YIO_LOG_INFO("PCBA Ids info: " << jsonToString(contentJson));

        return std::make_optional<PCBAIds>(contentJson);
    }

    PCBAInfo PCBAInfo::fromJson(const Json::Value& infoJson) {
        PCBAInfo info;
        info.name = infoJson["name"].asString();
        if (infoJson.isMember("error")) {
            info.error = infoJson["error"].asString();
            return info;
        }
        info.structVersion = tryGetInt(infoJson, "struct_version", -1);
        info.productModel = tryGetInt(infoJson, "model", -1);
        info.boardCode = tryGetInt(infoJson, "board_code", -1);
        info.hex = infoJson["hex"].asString();
        info.date = infoJson["date"].asString();
        info.revision.hi = tryGetInt(infoJson["revision"], "hi", -1);
        info.revision.mid = tryGetInt(infoJson["revision"], "mid", -1);
        info.revision.lo = tryGetInt(infoJson["revision"], "lo", -1);
        info.assembly = infoJson["assembly"].asString();
        return info;
    }

    Json::Value PCBAInfo::toJson(const PCBAInfo& info) {
        Json::Value json;
        json["name"] = info.name;
        if (info.hasError()) {
            json["error"] = *info.error;
            return json;
        }
        json["struct_version"] = info.structVersion;
        json["model"] = info.productModel;
        json["board_code"] = info.boardCode;
        json["hex"] = info.hex;
        json["date"] = info.date;
        json["revision"]["hi"] = info.revision.hi;
        json["revision"]["mid"] = info.revision.mid;
        json["revision"]["lo"] = info.revision.lo;

        if (!info.assembly.empty()) {
            json["assembly"] = info.assembly;
        }

        return json;
    }

    bool PCBAInfo::hasError() const {
        return error.has_value();
    }

    PCBAIds::PCBAIds(const Json::Value& pcbaInfoArray) {
        Y_VERIFY(pcbaInfoArray.isArray());

        for (Json::Value::ArrayIndex i = 0; i < pcbaInfoArray.size(); ++i) {
            auto info = PCBAInfo::fromJson(pcbaInfoArray[i]);
            const auto name = info.name;
            infos_[name] = std::move(info);
        }
    }

    bool PCBAIds::hasPCBA(const std::string& pcba) const {
        return infos_.find(pcba) != infos_.end();
    }

    const PCBAInfo& PCBAIds::getPCBA(const std::string& pcba) const {
        return infos_.at(pcba);
    }

    Json::Value PCBAIds::toJson() const {
        Json::Value array = Json::arrayValue;
        for (const auto& [name, pcba] : infos_) {
            array.append(PCBAInfo::toJson(pcba));
        }
        return array;
    }

    PCBAIdsMetricSender::PCBAIdsMetricSender(const Json::Value& payload,
                                             std::shared_ptr<YandexIO::ITelemetry> telemetry,
                                             std::chrono::seconds period) {
        sender_ = std::make_unique<PeriodicExecutor>([telemetry{std::move(telemetry)},
                                                      payloadStr{jsonToString(payload)}]() {
            telemetry->reportEvent("pcbaIdsInfo", payloadStr);
        }, period, PeriodicExecutor::PeriodicType::CALLBACK_FIRST);
    }

} // namespace quasar
