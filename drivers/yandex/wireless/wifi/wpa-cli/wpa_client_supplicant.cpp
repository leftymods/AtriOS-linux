#include "wpa_client_supplicant.h"

#include <yandex_io/libs/logging/logging.h>

#include <functional>
#include <string>

extern "C" {
#include <contrib/restricted/patched/hostap_client/src/utils/os.h>
#include <contrib/restricted/patched/hostap_client/src/utils/eloop.h>
#include <contrib/restricted/patched/hostap_client/wpa_supplicant/wpa_cli.h>
}

using namespace quasar;

template <typename T>
struct Callback;

template <typename Ret, typename... Params>
struct Callback<Ret(Params...)> {
    template <typename... Args>
    static Ret callback(Args... args) {
        func(args...);
    }
    static std::function<Ret(Params...)> func;
};

template <typename Ret, typename... Params>
std::function<Ret(Params...)> Callback<Ret(Params...)>::func;

WpaClientSupplicant::WpaClientSupplicant(const std::string& ifdir)
    : ctrl_ifname_(nullptr)
    , ctrl_ifname_dir_(ifdir)
{
    if (os_program_init()) {
        throw std::runtime_error("os_program_init() failed");
    }

    if (eloop_init()) {
        throw std::runtime_error("eloop_init() failed");
    }

    eloop_register_signal_terminate(wpa_cli_terminate, nullptr);
}

WpaClientSupplicant::~WpaClientSupplicant()
{
    eloop_terminate();
    if (eloop_.joinable()) {
        eloop_.join();
    }
}

void WpaClientSupplicant::setCallback(std::function<void(const char*)> fn)
{
    Callback<void(const char*)>::func = fn;
    wpa_monitor_callback_t func = static_cast<wpa_monitor_callback_t>(Callback<void(const char*)>::callback);
    wpa_cli_set_monitor_callback(func);
}

void WpaClientSupplicant::clearCallback()
{
    wpa_cli_clear_monitor_callback();
}

void WpaClientSupplicant::startThread()
{
    eloop_ = std::thread(&WpaClient::loopTask, this);
}

void WpaClientSupplicant::loopTask()
{
    wpa_cli_set_default_ifname_dir(ctrl_ifname_dir_.c_str());
    ctrl_ifname_ = wpa_cli_set_default_ifname();

    YIO_LOG_DEBUG("Wpa ctrl interface: " << ctrl_ifname_);

    /* From wpa_cli_interactive */
    eloop_register_timeout(0, 0, wpa_cli_try_connection, nullptr, nullptr);
    eloop_run();
    eloop_cancel_timeout(wpa_cli_try_connection, nullptr, nullptr);

    wpa_cli_list_flush();
    eloop_cancel_timeout(wpa_cli_ping, nullptr, nullptr);
    wpa_cli_close_connection();
    /* End from wpa_cli_interactive */

    os_free((void*)ctrl_ifname_);
    eloop_destroy();
    wpa_cli_cleanup();
}

/* Warning! returning buffer is static string inside contrib/wpa_supplicant library
 * It used twice for every command inside wpa_cli.c
 * Firstly it's filled with payload and then received answer also will be written in it.
 * so comand+returningbuffer MUST be guarded
 */
const char* WpaClientSupplicant::buffer() const {
    return wpa_cli_get_buffer();
}

std::string WpaClientSupplicant::scan() const {
    WpaGuard guard;
    if (wpa_cli_cmd_scan() != 0) {
        throw WpaClientException("scan failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::listNetworks() const {
    WpaGuard guard;
    if (wpa_cli_cmd_list_networks() != 0) {
        throw WpaClientException("listNetworks failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::addNetwork() const {
    WpaGuard guard;
    if (wpa_cli_cmd_add_network() != 0) {
        throw WpaClientException("addNetwork failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::selectNetwork(int networkId) const {
    WpaGuard guard;
    if (wpa_cli_cmd_select_network(networkId) != 0) {
        throw WpaClientException("selectNetwork failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::enableNetwork(int networkId) const {
    WpaGuard guard;
    if (wpa_cli_cmd_enable_network(networkId) != 0) {
        throw WpaClientException("enableNetwork failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::disableNetwork(int networkId) const {
    WpaGuard guard;
    if (wpa_cli_cmd_disable_network(networkId) != 0) {
        throw WpaClientException("disableNetwork failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::removeNetwork(int networkId) const {
    WpaGuard guard;
    if (wpa_cli_cmd_remove_network(networkId) != 0) {
        throw WpaClientException("removeNetwork failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::reloadConfig() const {
    WpaGuard guard;
    if (wpa_cli_cmd_reconfigure() != 0) {
        throw WpaClientException("reloadConfig failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::saveConfig() const {
    WpaGuard guard;
    if (wpa_cli_cmd_save_config() != 0) {
        throw WpaClientException("saveConfig failed");
    }
    return buffer();
}

bool WpaClientSupplicant::isAttached() const {
    WpaGuard guard;
    return wpa_cli_is_attached() == 1;
}

std::string WpaClientSupplicant::status(char* arg) const {
    WpaGuard guard;
    if (wpa_cli_cmd_status(arg) != 0) {
        throw WpaClientException("status failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::signalPoll() const {
    WpaGuard guard;
    if (wpa_cli_cmd_signal_poll() != 0) {
        throw WpaClientException("signalPoll failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::scanResults() const {
    WpaGuard guard;
    if (wpa_cli_cmd_scan_results() != 0) {
        throw WpaClientException("scanResults failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::setNetwork(int networkId, const char* varname, const char* value) const {
    WpaGuard guard;
    if (wpa_cli_cmd_set_network(networkId, varname, value) != 0) {
        throw WpaClientException("setNetwork failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::getNetwork(int networkId, const char* varname) const {
    WpaGuard guard;
    if (wpa_cli_cmd_get_network(networkId, varname) != 0) {
        throw WpaClientException("getNetwork failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::disconnect() const {
    WpaGuard guard;
    if (wpa_cli_cmd_disconnect() != 0) {
        throw WpaClientException("disconnect failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::reconnect() const {
    WpaGuard guard;
    if (wpa_cli_cmd_reconnect() != 0) {
        throw WpaClientException("reconnect failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::reassociate() const {
    WpaGuard guard;
    if (wpa_cli_cmd_reassociate() != 0) {
        throw WpaClientException("reassociate failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::enableAllNetworks() const {
    WpaGuard guard;
    if (wpa_cli_cmd_enable_all_networks() != 0) {
        throw WpaClientException("enableAllNetworks failed");
    }
    return buffer();
}

std::string WpaClientSupplicant::disableAllNetworks() const {
    WpaGuard guard;
    if (wpa_cli_cmd_disable_all_networks() != 0) {
        throw WpaClientException("disableAllNetworks failed");
    }
    return buffer();
}

WpaClientSupplicant::WpaGuard::WpaGuard() {
    eloop_lock();
}

WpaClientSupplicant::WpaGuard::~WpaGuard() {
    eloop_unlock();
}
