#pragma once

#include <speechkit/SoundLogger.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>

namespace AudioSender {

    class LogSendingResultSink: public SpeechKit::SoundLogger::ResultSink {
    public:
        using Callback = std::function<void(bool, const std::string&, const std::vector<std::string>&)>;
        using SharedPtr = std::shared_ptr<LogSendingResultSink>;

        LogSendingResultSink(const Callback& callback);
        void setRequiredCount(int count);
        std::future_status waitFor(std::chrono::milliseconds timeout);
        bool getResult();

    private:
        void onSuccess(const std::string& id, const std::vector<std::string>& messageIds) override;
        void onFail(const std::string& id, const std::string& error) override;

    private:
        Callback callback;
        std::atomic<int> successCount{0}, failCount{0};
        std::atomic<int> requiredCount{-1};
        std::promise<void> promise;
    };

} // namespace AudioSender
