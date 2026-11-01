#pragma once

#include <string>
#include <map>
#include <optional>

#include <yandex_io/libs/telemetry/telemetry.h>
#include <yandex_io/libs/threading/periodic_executor.h>

#include <json/json.h>

namespace quasar {

    class PCBAIds;
    /**
 * Try to load pcba ids info. returns nullopt on error
 */
    std::optional<PCBAIds> loadPCBAIds(const std::string& path);

    struct PCBAInfo {
    public:
        static PCBAInfo fromJson(const Json::Value& pcbaInfoJson);
        static Json::Value toJson(const PCBAInfo& pcbaInfo);
        bool hasError() const;

        std::string name;
        std::string hex;
        std::string date;
        int productModel{-1};
        int structVersion{-1};
        int boardCode{-1};
        struct Revision {
            int hi{-1};
            int mid{-1};
            int lo{-1};
        };
        Revision revision;
        std::string assembly;

        std::optional<std::string> error;
    };

    class PCBAIds {
    public:
        // should be an array (will be checked by Y_VERIFY)
        explicit PCBAIds(const Json::Value& pcbaInfoArray);

        bool hasPCBA(const std::string& pcba) const;
        const PCBAInfo& getPCBA(const std::string& pcba) const;

        // build a Json array with pcba infos (mirror for original json)
        Json::Value toJson() const;

    private:
        std::map<std::string, PCBAInfo> infos_;
    };

    // simple wraper for PeriodicExutor that sends pcbaIdsInfo metric
    // send metrics twice a day by default
    class PCBAIdsMetricSender {
    public:
        PCBAIdsMetricSender(const Json::Value& payload,
                            std::shared_ptr<YandexIO::ITelemetry> telemetry,
                            std::chrono::seconds period = std::chrono::hours(12));

    private:
        std::unique_ptr<PeriodicExecutor> sender_;
    };

} // namespace quasar
