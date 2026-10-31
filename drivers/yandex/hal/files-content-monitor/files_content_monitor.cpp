#include "files_content_monitor.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("files_monitor");

using namespace std::chrono_literals;
using namespace quasar;

FilesContentMonitor::FilesContentMonitor(std::unique_ptr<ICallbackQueue> executor)
    : executor_(std::move(executor))
{
}

void FilesContentMonitor::addListener(std::weak_ptr<IListener> listener) {
    executor_->add([this, listener{std::move(listener)}]() mutable {
        listeners_.push_back(std::move(listener));
        if (auto slistener = listeners_.back().lock(); slistener) {
            for (const auto& [node, data] : nodesData_) {
                slistener->onData(node, data);
            }
        }
    });
}

void FilesContentMonitor::addNode(const Node& node) {
    executor_->add([this, node{node}]() mutable {
        updateData(std::move(node));
    });
}

void FilesContentMonitor::updateData(Node node) {
    Y_ENSURE_THREAD(executor_);

    Json::Value data;
    try {
        switch (node.type) {
            case Node::Type::STRING: {
                data["data"] = getFileContent(node.path);
                break;
            }
            case Node::Type::INT: {
                data["data"] = std::atoi(getFileContent(node.path).c_str());
                break;
            }
        }
    } catch (const std::runtime_error& e) {
        YIO_LOG_WARN("Failed to get node content. Error: " << e.what());
        data["error"] = e.what();
    }

    for (const auto& wlistener : listeners_) {
        if (const auto listener = wlistener.lock()) {
            listener->onData(node.name, data);
        }
    }

    nodesData_[node.name] = std::move(data);

    if (node.mode == Node::MonitoringMode::PERIODIC && node.period.count()) {
        executor_->addDelayed([this, node{std::move(node)}]() mutable {
            updateData(std::move(node));
        }, node.period);
    }
}
