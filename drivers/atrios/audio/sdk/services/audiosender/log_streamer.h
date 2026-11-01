#pragma once

#include "working_thread.h"

#include <memory>
#include <string>

namespace AudioSender {

    class LogStreamerState;

    class LogStreamer: public std::enable_shared_from_this<LogStreamer> {
    public:
        using SharedPtr = std::shared_ptr<LogStreamer>;
        static SharedPtr getInstance();
        LogStreamer();
        void setServerAddress(const std::string& address);
        void start();
        void stop();
        void writeLog(const std::string& log);

    private:
        using TWorkingThread = WorkingThread<LogStreamer, LogStreamerState>;
        TWorkingThread::SharedPtr workingThread;
    };

} // namespace AudioSender
