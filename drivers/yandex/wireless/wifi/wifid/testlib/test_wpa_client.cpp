#include "test_wpa_client.h"

#include <yandex_io/libs/logging/logging.h>

#include <sys/time.h>

using namespace quasar;

TestWpaClient::~TestWpaClient()
{
    loopTaskRun_ = false;
    monitorWait_.notify_all();
    if (eloop_.valid())
        eloop_.wait();
}

void TestWpaClient::loopTask()
{
    std::unique_lock<std::mutex> lk(monitorLock_);
    while (loopTaskRun_)
    {
        struct timeval tv;
        uint64_t now;
        uint64_t delay = 1000;
        for (auto e = monitorFifo_.begin(); e != monitorFifo_.end();)
        {
            gettimeofday(&tv, nullptr);
            now = tv.tv_sec * 1000 + tv.tv_usec / 1000;
            if (e->first > now)
            {
                delay = e->first - now;
                break;
            }
            while (!e->second.empty())
            {
                auto fn = e->second.front();
                e->second.pop();
                lk.unlock();
                std::string buf;
                {
                    std::lock_guard<std::mutex> g(lock_);
                    fn(buffer_);
                    buf = buffer_;
                }
                callback_(buf.c_str());
                lk.lock();
            }
            e = monitorFifo_.erase(e);
        }
        monitorWait_.wait_for(lk, std::chrono::milliseconds(delay));
    }
}

void TestWpaClient::setCallback(std::function<void(const char*)> fn)
{
    std::lock_guard<std::mutex> g(lock_);
    callback_ = fn;
}

void TestWpaClient::clearCallback()
{
    std::lock_guard<std::mutex> g(lock_);
    callback_ = nullptr;
}

void TestWpaClient::startThread()
{
    loopTaskRun_ = true;
    std::lock_guard<std::mutex> g(lock_);
    eloop_ = std::async(std::launch::async, std::bind(&WpaClient::loopTask, this));
}

bool TestWpaClient::setBuffer(const std::string& buffer)
{
    std::lock_guard<std::mutex> g(lock_);
    buffer_ = buffer;
    return true;
}

void TestWpaClient::clearCommands()
{
    std::lock_guard<std::mutex> g(lock_);
    wpaCmd_.clear();
    wpaCmdNetwork_.clear();
    wpaCmdStatus_ = {};
    wpaCmdSetNetwork_ = {};
    wpaCmdGetNetwork_ = {};
}

void TestWpaClient::scheduleMonitorEvent(uint32_t delayMs, monitorFn fn)
{
    monitorLock_.lock();
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t now = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    monitorFifo_[now + delayMs].push(fn);
    monitorLock_.unlock();
    monitorWait_.notify_all();
}

void TestWpaClient::onCommand(CmdType cmd, wpaCmdFn fn)
{
    std::lock_guard<std::mutex> g(lock_);

    switch (cmd)
    {
        case CmdType::SCAN:
        case CmdType::LIST_NETWORK:
        case CmdType::ADD_NETWORK:
        case CmdType::SIGNAL_POLL:
        case CmdType::SAVE:
        case CmdType::SCAN_RESULTS:
        case CmdType::IS_ATTACHED:
        case CmdType::REASSOCIATE:
            wpaCmd_.emplace(cmd, fn);
            break;

        default:
            throw std::runtime_error("Wrong CmdType");
    }
}

void TestWpaClient::onCommand(CmdType cmd, wpaCmdNetworkFn fn)
{
    std::lock_guard<std::mutex> g(lock_);

    switch (cmd)
    {
        case CmdType::SELECT_NETWORK:
        case CmdType::ENABLE_NETWORK:
        case CmdType::DISABLE_NETWORK:
        case CmdType::REMOVE_NETWORK:
        case CmdType::DISABLE_ALL_NETWORKS:
        case CmdType::ENABLE_ALL_NETWORKS:
            wpaCmdNetwork_.emplace(cmd, fn);
            break;

        default:
            throw std::runtime_error("Wrong CmdType");
    }
}

void TestWpaClient::onCommand(CmdType cmd, wpaCmdStatusFn fn)
{
    std::lock_guard<std::mutex> g(lock_);

    if (cmd != CmdType::STATUS)
        throw std::runtime_error("Wrong CmdType");

    wpaCmdStatus_ = fn;
}

void TestWpaClient::onCommand(CmdType cmd, wpaCmdGetNetworkFn fn)
{
    std::lock_guard<std::mutex> g(lock_);

    if (cmd != CmdType::GET_NETWORK)
        throw std::runtime_error("Wrong CmdType");

    wpaCmdGetNetwork_ = fn;
}

void TestWpaClient::onCommand(CmdType cmd, wpaCmdSetNetworkFn fn)
{
    std::lock_guard<std::mutex> g(lock_);

    if (cmd != CmdType::SET_NETWORK)
        throw std::runtime_error("Wrong CmdType");

    wpaCmdSetNetwork_ = fn;
}

const char* TestWpaClient::buffer() const {
    std::lock_guard<std::mutex> g(lock_);
    return buffer_.c_str();
}

std::string TestWpaClient::scan() const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmd_.find(CmdType::SCAN);
    if (fn == wpaCmd_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Scan failed");
    }

    if (fn->second(buffer_)) {
        return buffer_;
    }
    throw WpaClientException("Scan failed, at the end");
}

std::string TestWpaClient::listNetworks() const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmd_.find(CmdType::LIST_NETWORK);
    if (fn == wpaCmd_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Listing network failed");
    }

    if (fn->second(buffer_)) {
        return buffer_;
    }
    throw WpaClientException("Listing network failed, at the end");
}

std::string TestWpaClient::addNetwork() const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmd_.find(CmdType::ADD_NETWORK);
    if (fn == wpaCmd_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Adding network failed");
    }

    if (fn->second(buffer_)) {
        return buffer_;
    }
    throw WpaClientException("Adding network failed, at the end");
}

std::string TestWpaClient::selectNetwork(int networkId) const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmdNetwork_.find(CmdType::SELECT_NETWORK);
    if (fn == wpaCmdNetwork_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Selecting network " + std::to_string(networkId) + " failed");
    }

    if (fn->second(buffer_, networkId)) {
        return buffer_;
    }
    throw WpaClientException("Selecting network " + std::to_string(networkId) + " failed, at the end");
}

std::string TestWpaClient::enableNetwork(int networkId) const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmdNetwork_.find(CmdType::ENABLE_NETWORK);
    if (fn == wpaCmdNetwork_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Enabling network " + std::to_string(networkId) + " failed");
    }

    if (fn->second(buffer_, networkId)) {
        return buffer_;
    }
    throw WpaClientException("Enabling network " + std::to_string(networkId) + " failed, at the end");
}

std::string TestWpaClient::disableNetwork(int networkId) const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmdNetwork_.find(CmdType::DISABLE_NETWORK);
    if (fn == wpaCmdNetwork_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Disabling network " + std::to_string(networkId) + " failed");
    }

    if (fn->second(buffer_, networkId)) {
        return buffer_;
    }
    throw WpaClientException("Disabling network " + std::to_string(networkId) + " failed, at the end");
}

std::string TestWpaClient::removeNetwork(int networkId) const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmdNetwork_.find(CmdType::REMOVE_NETWORK);
    if (fn == wpaCmdNetwork_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Removing network " + std::to_string(networkId) + " failed");
    }

    if (fn->second(buffer_, networkId)) {
        return buffer_;
    }
    throw WpaClientException("Removing network " + std::to_string(networkId) + " failed, at the end");
}

std::string TestWpaClient::reloadConfig() const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmd_.find(CmdType::RECONFIGURE);
    if (fn == wpaCmd_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Config reloading failed");
    }

    if (fn->second(buffer_)) {
        return buffer_;
    }
    throw WpaClientException("Config reloading failed, at the end");
}

std::string TestWpaClient::saveConfig() const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmd_.find(CmdType::SAVE);
    if (fn == wpaCmd_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Config saving failed");
    }

    if (fn->second(buffer_)) {
        return buffer_;
    }
    throw WpaClientException("Config saving failed, end");
}

bool TestWpaClient::isAttached() const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmd_.find(CmdType::IS_ATTACHED);
    if (fn == wpaCmd_.end())
    {
        return false;
    }

    if (fn->second(buffer_)) {
        return true;
    }
    return false;
}

std::string TestWpaClient::status(char* arg) const {
    std::lock_guard<std::mutex> g(lock_);

    if (!wpaCmdStatus_)
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Status call failed, args: " + std::string(arg));
    }

    if (wpaCmdStatus_(buffer_, arg)) {
        return buffer_;
    }
    throw WpaClientException("Status call failed at the end, args: " + std::string(arg));
}

std::string TestWpaClient::signalPoll() const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmd_.find(CmdType::SIGNAL_POLL);
    if (fn == wpaCmd_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("");
    }

    if (fn->second(buffer_)) {
        return buffer_;
    }
    throw WpaClientException("");
}

std::string TestWpaClient::scanResults() const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmd_.find(CmdType::SCAN_RESULTS);
    if (fn == wpaCmd_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Scan results call failed");
    }

    if (fn->second(buffer_)) {
        return buffer_;
    }
    throw WpaClientException("Scan results call failed, at the end");
}

std::string TestWpaClient::setNetwork(int networkId, const char* varname, const char* value) const {
    std::lock_guard<std::mutex> g(lock_);

    if (!wpaCmdSetNetwork_)
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Set network " + std::to_string(networkId) + " failed");
    }

    if (wpaCmdSetNetwork_(buffer_, networkId, varname, value)) {
        return buffer_;
    }
    throw WpaClientException("Set network " + std::to_string(networkId) + " failed, at the end");
}

std::string TestWpaClient::getNetwork(int networkId, const char* varname) const {
    std::lock_guard<std::mutex> g(lock_);

    if (!wpaCmdGetNetwork_)
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Get network " + std::to_string(networkId) + " failed");
    }

    if (wpaCmdGetNetwork_(buffer_, networkId, varname)) {
        return buffer_;
    }
    throw WpaClientException("Get network " + std::to_string(networkId) + " failed, at the end");
}

std::string TestWpaClient::reassociate() const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmd_.find(CmdType::REASSOCIATE);
    if (fn == wpaCmd_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Reassociating failed");
    }

    if (fn->second(buffer_)) {
        return buffer_;
    }
    throw WpaClientException("Reassociating failed, at the end");
}

std::string TestWpaClient::disconnect() const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmd_.find(CmdType::DISCONNECT);
    if (fn == wpaCmd_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Disconnect failed");
    }

    if (fn->second(buffer_)) {
        return buffer_;
    }
    throw WpaClientException("Disconnect failed");
}

std::string TestWpaClient::reconnect() const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmd_.find(CmdType::RECONNECT);
    if (fn == wpaCmd_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Reconnect failed");
    }

    if (fn->second(buffer_)) {
        return buffer_;
    }
    throw WpaClientException("Reconnect failed, at the end");
}

std::string TestWpaClient::enableAllNetworks() const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmdNetwork_.find(CmdType::ENABLE_ALL_NETWORKS);
    if (fn == wpaCmdNetwork_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Enabling all networks failed");
    }

    if (fn->second(buffer_, 0)) { // Second param is a fake one
        return buffer_;
    }
    throw WpaClientException("Enabling all networks failed, at the end");
}

std::string TestWpaClient::disableAllNetworks() const {
    std::lock_guard<std::mutex> g(lock_);

    auto fn = wpaCmdNetwork_.find(CmdType::DISABLE_ALL_NETWORKS);
    if (fn == wpaCmdNetwork_.end())
    {
        buffer_.assign("FAIL\n");
        throw WpaClientException("Disabling all networks failed");
    }

    if (fn->second(buffer_, 0)) { // Second param is a fake one
        return buffer_;
    }
    throw WpaClientException("Disabling all networks failed, at the end");
}
