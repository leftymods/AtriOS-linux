#include "log_sending_resul_sink.h"

#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("audio_sender");

using namespace AudioSender;

LogSendingResultSink::LogSendingResultSink(const Callback& callback)
    : callback(callback)
{
}

void LogSendingResultSink::setRequiredCount(int count) {
    promise = std::promise<void>();
    requiredCount.store(count);
}

std::future_status LogSendingResultSink::waitFor(std::chrono::milliseconds timeout) {
    if (failCount.load() > 0 || requiredCount.load() == successCount.load()) {
        return std::future_status::ready;
    }
    if (successCount.load() > requiredCount.load()) {
        YIO_LOG_ERROR_EVENT("LogSendingResultSink.InvalidCounters", "required: " << requiredCount.load() << ", success: " << successCount.load());
    }

    auto future = promise.get_future();
    return future.wait_for(timeout);
}

bool LogSendingResultSink::getResult() {
    if (requiredCount.load() == -1) {
        throw std::runtime_error("Set required count before getting result");
    }

    if (failCount.load() > 0) {
        return false;
    }

    if (requiredCount.load() == successCount.load()) {
        return true;
    }

    promise.get_future().wait();
    return failCount.load() == 0;
}

void LogSendingResultSink::onSuccess(const std::string& /*id*/, const std::vector<std::string>& messageIds) {
    ++successCount;

    callback(true, "", messageIds);

    if (requiredCount.load() != -1 && requiredCount.load() == successCount.load()) {
        promise.set_value();
    }
}

void LogSendingResultSink::onFail(const std::string& /*id*/, const std::string& error) {
    ++failCount;

    callback(false, error, {});

    if (requiredCount.load() != -1) {
        promise.set_value();
    }
}
