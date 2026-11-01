#pragma once

#include <yandex_io/libs/threading/i_callback_queue.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <json/json.h>

class FilesContentMonitor {
public:
    class IListener {
    public:
        virtual void onData(const std::string& nodeName, const Json::Value& data) = 0;

        virtual ~IListener() = default;
    };

    struct Node {
        std::string path;
        std::string name;

        enum class Type {
            INT,
            STRING
        };
        Type type;

        enum class MonitoringMode {
            ONCE,
            PERIODIC
        };
        MonitoringMode mode;

        std::chrono::milliseconds period;
    };

    FilesContentMonitor(std::unique_ptr<quasar::ICallbackQueue> executor);

    void addNode(const Node& node);
    void addListener(std::weak_ptr<IListener> listener);

private:
    void updateData(Node node);

    const std::unique_ptr<quasar::ICallbackQueue> executor_;

    std::map<std::string, Json::Value> nodesData_;

    std::vector<std::weak_ptr<IListener>> listeners_;
};
