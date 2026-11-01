#include "log_streamer.h"

#include "log_streamer_state.h"

using namespace AudioSender;

LogStreamer::SharedPtr LogStreamer::getInstance() {
    static LogStreamer::SharedPtr instance = std::make_shared<LogStreamer>();
    return instance;
}

LogStreamer::LogStreamer()
    : workingThread(std::make_shared<TWorkingThread>())
{
}

void LogStreamer::setServerAddress(const std::string& address) {
    workingThread->dispatchAsync(shared_from_this(),
                                 [address](LogStreamerState::SharedPtr state) {
                                     state->onSetServerAddress(address);
                                 });
}

void LogStreamer::start() {
    workingThread->dispatchAsync(shared_from_this(),
                                 [](LogStreamerState::SharedPtr state) {
                                     state->onStart();
                                 });
}

void LogStreamer::stop() {
    workingThread->dispatchAsync(shared_from_this(),
                                 [](LogStreamerState::SharedPtr state) {
                                     state->onStop();
                                 });
}

void LogStreamer::writeLog(const std::string& log) {
    workingThread->dispatchAsync(shared_from_this(),
                                 [log](LogStreamerState::SharedPtr state) {
                                     state->onWriteLog(log);
                                 });
}
