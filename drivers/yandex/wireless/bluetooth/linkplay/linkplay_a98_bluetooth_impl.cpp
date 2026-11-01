#include "linkplay_a98_bluetooth_impl.h"

#include <smart_devices/third_party/linkplay_bluetooth/linkplay_bluetooth_interface.h>
#include <smart_devices/third_party/linkplay/linkplay_resample_interface.h>

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <atomic>
#include <cstring>
#include <errno.h>
#include <fstream>
#include <functional>
#include <sstream>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

YIO_DEFINE_LOG_MODULE("bluetooth");

using namespace quasar;

namespace {

    void set_bt_transfer(int enable)
    {
        /**
     * By changing state of this sysfs node bluetoothd notifies Stream Producer, should it (producer) write to
     * FIFO or not. In current implementation the stream provider is libsoft_prep.so which is used by 'audiod' daemon
     * So when /sys/class/tdm_state/amp_table is set up to 0, libsoft_prep starts to write to /tmp/loop_capture_pcm
     * FIFO (which is created in run.sh). And when /sys/.../amp_table is set up to 1, libsoft_prep do not send Stream
     * buffers.
     */
        static constexpr const char* BT_STATE_SYSFS_NODE = "/sys/class/tdm_state/amp_table";
        std::fstream f(BT_STATE_SYSFS_NODE);
        if (f.good()) {
            if (enable) {
                f << 0;
            } else {
                f << 1;
            }
        } else {
            std::stringstream ss;
            /* NOTE: It seems like it is actually amplifier sysfs node, but it is also used for bt stream out */
            ss << "Can't open Bluetooth Stream Out SYSFS node: " << BT_STATE_SYSFS_NODE;
            YIO_LOG_ERROR_EVENT("LinkplayA98BluetoothImpl.OpenAmpTableFailed", ss.str());
            throw std::runtime_error(ss.str());
        }
    }

    enum {
        COMMAND_START_SERVICES = 1,
        COMMAND_SET_VISIBILITY,
        COMMAND_START_SCAN,
        COMMAND_CANCEL_SCAN,
        COMMAND_SET_VOLUME_ABS,
        COMMAND_PLAYBACK_START,
        COMMAND_PLAYBACK_PAUSE,
        COMMAND_PLAYBACK_PREV,
        COMMAND_PLAYBACK_NEXT,
        COMMAND_DISCONNECT_FROM,
        COMMAND_DISCONNECT_ALL,
        COMMAND_SOURCE_CONNECT,
        COMMAND_START_STREAM,
        COMMAND_STOP_STREAM
    };

} // namespace

std::string LinkplayA98BluetoothImpl::msgTypeToString(const int msgType)
{
    switch (msgType) {
        case BLUETOOTH_SERVICES_EVENT: {
            return "BLUETOOTH_SERVICES_EVENT";
        }
        case BLUETOOTH_SEC_LINK_EVENT: {
            return "BLUETOOTH_SEC_LINK_EVENT";
        }
        case BLUETOOTH_SINK_EVENT: {
            return "BLUETOOTH_SINK_EVENT";
        }
        case BLUETOOTH_SOURCE_EVENT: {
            return "BLUETOOTH_SOURCE_EVENT";
        }
        default: {
        }
    }
    return "";
}

LinkplayA98BluetoothImpl::LinkplayA98BluetoothImpl(const std::string& name)
    : Bluetooth(name)
    , threadExists_(true)
{
    /* Todo: handle Avrcp start/stop to avoid annoying extra stream */
    streamAlsaWriter_.playBuffers();
    playingTimer_ = std::make_unique<LinuxTimer>([this]() {
        playingTimeout();
    });
    start();
    streamSender_.runCaptureThread();

    powerState_ = PowerState::ON;
    /* Notify user that Bluetooth Powered On*/
    EventResult res;
    res.result = EventResult::Result::OK;
    baseEventCallbackLocked(BaseEvent::POWER_ON, res);
}

LinkplayA98BluetoothImpl::~LinkplayA98BluetoothImpl()
{
    stopStream();
    disconnectAll(BtRole::SOURCE);
    setVisibility(false, false);
    powerOffImpl();
    {
        // wait until all bt sources will be disconnected, so handleWriteData won't be called
        // after destruction
        std::unique_lock lock(sourceConnMutex_);
        condVar_.wait_for(lock, std::chrono::seconds(5), [this]() {
            return curSourceConnMacs_.empty();
        });
    }
    set_bt_transfer(0);

    threadExists_ = false;
    /* interrupt thread */
    linkplay_bluetooth_schedule_event();

    btThread_.join();

    playingTimer_->stop();
    playingTimer_.reset();

    streamSender_.stopCaptureThread();
}

void LinkplayA98BluetoothImpl::setBsaServerMode(int mode) {
    if (mode == BSA_ENABLED) {
        YIO_LOG_INFO("Start bsa_server");
    } else {
        YIO_LOG_INFO("Stop bsa_server");
    }
    std::stringstream ss;
    ss << "/system/vendor/quasar/bsa_server_run.sh " << mode;
    system(ss.str().c_str());
}

int LinkplayA98BluetoothImpl::powerOff()
{
    return powerOffImpl();
}

int LinkplayA98BluetoothImpl::powerOffImpl()
{
    /* linkplay API does not have powerOn and powerOff functions
     * so prevent like it's turned off
     */
    powerState_ = PowerState::OFF;
    perform(BtAppCommands::DISCONNECT_ALL);
    Bluetooth::EventResult res;
    res.result = Bluetooth::EventResult::Result::OK;
    baseEventCallbackLocked(Bluetooth::BaseEvent::POWER_OFF, res);
    return 0;
}

int LinkplayA98BluetoothImpl::powerOn()
{
    /* linkplay API does not have powerOn and powerOff functions
     * so prevent like it's turned on
     */
    powerState_ = PowerState::ON;
    Bluetooth::EventResult res;
    res.result = Bluetooth::EventResult::Result::OK;
    baseEventCallbackLocked(Bluetooth::BaseEvent::POWER_ON, res);
    return 0;
}

Bluetooth::PowerState LinkplayA98BluetoothImpl::getPowerState() const {
    return powerState_;
}

int LinkplayA98BluetoothImpl::startStream()
{
    return perform(BtAppCommands::START_STREAM);
}

int LinkplayA98BluetoothImpl::stopStream()
{
    return perform(BtAppCommands::STOP_STREAM);
}

int LinkplayA98BluetoothImpl::setVisibility(bool isDiscoverable, bool isConnectable)
{
    return perform(BtAppCommands::SET_VISIBILITY, isDiscoverable, isConnectable);
}

int LinkplayA98BluetoothImpl::scanNetworks()
{
    return perform(BtAppCommands::START_SCAN);
}

int LinkplayA98BluetoothImpl::stopScanNetworks()
{
    return perform(BtAppCommands::CANCEL_SCAN);
}

int LinkplayA98BluetoothImpl::pairWithSink(const Bluetooth::BtNetwork& network)
{
    return perform(BtAppCommands::SOURCE_CONNECT, network.addr);
}

int LinkplayA98BluetoothImpl::disconnectAll(BtRole role)
{
    std::lock_guard<std::mutex> guard(commandMutex_);
    BtCommand command;
    command.cmd = COMMAND_DISCONNECT_ALL;
    command.roleToDisconnect = role;
    /* TODO: move to addCommand method */
    commandsQueue_.push(command);

    /* Notify Bluetooth Thread about event */
    linkplay_bluetooth_schedule_event();
    return 0;
}

int LinkplayA98BluetoothImpl::asSinkPlayNext(const Bluetooth::BtNetwork& /* network */)
{
    YIO_LOG_INFO("Send AVRCP_NEXT to Source");
    linkplay_bluetooth_playback_next(BT_ROLE_SINK, 0);
    return 0;
}

int LinkplayA98BluetoothImpl::asSinkPlayPrev(const Bluetooth::BtNetwork& /* network */)
{
    YIO_LOG_INFO("Send AVRCP_PREV to Source");
    linkplay_bluetooth_playback_prev(BT_ROLE_SINK, 0);
    return 0;
}

int LinkplayA98BluetoothImpl::asSinkPlayStart(const Bluetooth::BtNetwork& /* network */)
{
    YIO_LOG_INFO("Send AVRCP_PLAY to Source");
    linkplay_bluetooth_playback_resume(BT_ROLE_SINK, 0);
    return 0;
}

int LinkplayA98BluetoothImpl::asSinkPlayPause(const Bluetooth::BtNetwork& /* network */)
{
    YIO_LOG_INFO("Send AVRCP_PAUSE to Source");
    linkplay_bluetooth_playback_pause(BT_ROLE_SINK, 0);
    return 0;
}

int LinkplayA98BluetoothImpl::asSinkSetVolumeAbs(int volume)
{
    perform(BtAppCommands::SET_VOLUME_ABS, volume);
    return 0;
}

static int string_to_bt_address(const char* addr, BD_ADDR* btAddr)
{
    int tmp[BD_ADDR_LEN];

    sscanf(addr, "%02x:%02x:%02x:%02x:%02x:%02x", &tmp[0], &tmp[1],
           &tmp[2], &tmp[3],
           &tmp[4], &tmp[5]);
    (*btAddr)[0] = tmp[0];
    (*btAddr)[1] = tmp[1];
    (*btAddr)[2] = tmp[2];
    (*btAddr)[3] = tmp[3];
    (*btAddr)[4] = tmp[4];
    (*btAddr)[5] = tmp[5];

    return 0;
}

std::set<Bluetooth::BtNetwork> LinkplayA98BluetoothImpl::getNetworksSetFromJson(const Json::Value& networks) {
    if (networks.empty()) {
        return std::set<Bluetooth::BtNetwork>();
    }

    std::set<Bluetooth::BtNetwork> networksSet;
    const int num = networks["num"].asInt();

    const Json::Value netList = networks["list"];

    if (!netList.isArray() && num != 0) {
        return std::set<Bluetooth::BtNetwork>();
    }
    for (int i = 0; i < num; ++i) {
        const Json::Value val = netList.get(static_cast<Json::ArrayIndex>(i),
                                            Json::Value());
        if (val.isNull()) {
            throw std::runtime_error("Invalid scan value: " + jsonToString(val));
        }

        BtNetwork net;
        net.addr = val["ad"].asString();
        net.name = val["name"].asString();

        const std::string role = val["role"].asString();
        if (role == "Audio Source") {
            net.role = Bluetooth::BtRole::SOURCE;
        } else if (role == "Audio Sink") {
            net.role = Bluetooth::BtRole::SINK;
        } else {
            net.role = Bluetooth::BtRole::UNKNOWN;
        }
        networksSet.insert(net);
    }
    return networksSet;
}

int LinkplayA98BluetoothImpl::eventCallback(bt_event_msg* msg, void* context)
{
    LinkplayA98BluetoothImpl* this_ = reinterpret_cast<LinkplayA98BluetoothImpl*>(context);

    YIO_LOG_DEBUG("Event callback. Msg Type: " << msgTypeToString(msg->msg_type));
    YIO_LOG_DEBUG("Event callback. Msg value: " << ((msg->msg_value != nullptr) ? msg->msg_value : "empty message"));
    Bluetooth::EventResult result;

    switch (msg->msg_type) {
        case BLUETOOTH_SERVICES_EVENT: {
            YIO_LOG_DEBUG("Service event: " << btServEvtStr((tBT_SERV_EVT)msg->notify_event));
            switch (msg->notify_event) {
                case BT_DISC_NEW_EVT: {
                    if (msg->msg_value == nullptr) {
                        return 0;
                    }

                    /* TODO: Hide using mutex? */
                    this_->networksJsonStr_ = std::string(msg->msg_value);

                    const Json::Value networks = parseJson(this_->networksJsonStr_);
                    result.scanResult = getNetworksSetFromJson(networks);
                    if (!result.scanResult.empty()) {
                        result.result = Bluetooth::EventResult::Result::OK;
                        this_->baseEventCallbackLocked(Bluetooth::BaseEvent::SCANNING, result);
                    }
                    break;
                }
                case BT_DISC_CMPL_EVT: {
                    /* Scan ended. */
                    const Json::Value networks = parseJson(this_->networksJsonStr_);
                    result.scanResult = getNetworksSetFromJson(networks);
                    if (!result.scanResult.empty()) {
                        result.result = Bluetooth::EventResult::Result::OK;
                    } else {
                        result.result = Bluetooth::EventResult::Result::ERROR;
                    }
                    this_->baseEventCallbackLocked(Bluetooth::BaseEvent::SCANNED, result);
                    break;
                }
                default: {
                    /* not handle msg->notify_event, Ignore */
                    break;
                }
            }
            break;
        }
        case BLUETOOTH_SEC_LINK_EVENT: {
            YIO_LOG_DEBUG("Sec Link event: " << btSecEvtStr((tBT_SEC_EVT)msg->notify_event));
            switch (msg->notify_event) {
                case BT_SEC_LINK_UP_EVT:
                    break;
                case BT_SEC_LINK_DOWN_EVT: {
                    if (msg->notify_value == 0x13) {
                        /* "Remote terminate bluetooth connection" */
                    } else if (msg->notify_value == 0x8) {
                        /*"BT connection timeout(beyond distance maybe)"! */
                        //auto reconnect
                    } else if (msg->notify_value == 0x16) {
                        /* "connection terminated by local host!" */
                    }
                    break;
                }
            }
            break;
        }
        case BLUETOOTH_SINK_EVENT: {
            switch (msg->notify_event) {
                case BT_SINK_CONNECTED_EVT: {
                    if (msg->notify_value == 0 && msg->msg_value != nullptr) {
                        Json::Value addrJson = parseJson(msg->msg_value);
                        if (!addrJson.isNull() && addrJson.isMember("bt_addr")) {
                            /* TODO: Find a way to fill name */
                            result.network.addr = addrJson["bt_addr"].asString();
                            result.network.role = Bluetooth::BtRole::SOURCE;
                            result.result = Bluetooth::EventResult::Result::OK;
                            this_->sinkEventCallbackLocked(Bluetooth::SinkEvent::CONNECTED, result);
                            std::scoped_lock guard(this_->sourceConnMutex_);
                            this_->curSourceConnMacs_.insert(addrJson["bt_addr"].asString());
                        }
                    }
                    break;
                }
                case BT_SINK_DISCONNECTED_EVT: {
                    if (msg->notify_value == 0 && msg->msg_value != nullptr) {
                        const Json::Value addrJson = parseJson(msg->msg_value);
                        // FIXME: turn off stream here
                        if (!addrJson.isNull() && addrJson.isMember("bt_addr")) {
                            /* TODO: Find a way to fill name */
                            result.network.addr = addrJson["bt_addr"].asString();
                            result.network.role = Bluetooth::BtRole::SOURCE;
                            result.result = Bluetooth::EventResult::Result::OK;
                            this_->sinkEventCallbackLocked(Bluetooth::SinkEvent::DISCONNECTED, result);
                            std::scoped_lock guard(this_->sourceConnMutex_);
                            auto it = this_->curSourceConnMacs_.find(addrJson["bt_addr"].asString());
                            if (it != this_->curSourceConnMacs_.end()) {
                                this_->curSourceConnMacs_.erase(it);
                            }
                            this_->condVar_.notify_one();
                        }
                    }
                    break;
                }
                case BT_SINK_SET_ABS_VOL_CMD_EVT: {
                    YIO_LOG_INFO("Set abs volume by bluetooth:" << msg->notify_value);
                    result.result = Bluetooth::EventResult::Result::OK;
                    result.avrcpEvent = AVRCP::CHANGE_VOLUME_ABS;
                    result.volumeAbs = msg->notify_value;
                    this_->sinkEventCallbackLocked(Bluetooth::SinkEvent::AVRCP_IN, result);
                    break;
                }
                case BT_SINK_REMOTE_RC_PLAY_EVT: {
                    break;
                }
                case BT_SINK_REMOTE_RC_PAUSE_EVT: {
                    break;
                }
                case BT_SINK_REMOTE_RC_STOP_EVT: {
                    break;
                }
                case BT_SINK_REMOTE_RC_TRACK_CHANGE: {
                    /* Request track info */
                    linkplay_bluetooth_get_element(0);
                    break;
                }
                case BT_SINK_GET_ELEM_ATTR_EVT: {
                    if (msg->msg_value == nullptr) {
                        break;
                    }
                    const std::string msgStr(msg->msg_value);
                    const auto optMsgJson = tryParseJson(msgStr);
                    if (!optMsgJson.has_value()) {
                        break;
                    }
                    YIO_LOG_INFO("Bt attrt data: " << msgStr);
                    const auto& msgJson = optMsgJson.value();
                    if (!msgJson.isMember("title")) {
                        // skip not track info
                        break;
                    }

                    result.result = EventResult::Result::OK;
                    result.avrcpEvent = AVRCP::TRACK_META_INFO;
                    if (msgJson.isMember("title") && msgJson["title"].isString()) {
                        result.trackInfo.title = msgJson["title"].asString();
                    }
                    if (msgJson.isMember("artist") && msgJson["artist"].isString()) {
                        result.trackInfo.artist = msgJson["artist"].asString();
                    }
                    if (msgJson.isMember("album") && msgJson["album"].isString()) {
                        result.trackInfo.album = msgJson["album"].asString();
                    }
                    if (msgJson.isMember("playtime") && msgJson["playtime"].isNumeric()) {
                        result.trackInfo.songLenMs = msgJson["playtime"].asInt();
                    }
                    result.trackInfo.currPosMs = -1;
                    /* Send track meta information to listeners */
                    this_->sinkEventCallbackLocked(SinkEvent::AVRCP_IN, result);
                    break;
                }
            }
            YIO_LOG_DEBUG("Sink event: " << btSinkEvtStr((tBT_SINK_EVT)msg->notify_event));
            break;
        }
        case BLUETOOTH_SOURCE_EVENT: {
            YIO_LOG_INFO("Source event: " << btSourceEvtStr((tBT_SOURCE_EVT)msg->notify_event));

            switch (msg->notify_event) {
                case BT_SOURCE_CONNECTED_EVT: {
                    /* msg value shouldn't be nullptr, it contains bt_addr */
                    result.result = Bluetooth::EventResult::Result::ERROR;
                    if (msg->notify_value == 0 && msg->msg_value != nullptr) {
                        BD_ADDR remoteBdAddr;
                        REMOTE_DEVICE_INFO remoteDeviceInfo;

                        try {
                            const Json::Value addrJson = parseJson(msg->msg_value);
                            /* Save paired sink */
                            this_->pairedSinkAddr_ = addrJson["bt_addr"].asString();
                            if (this_->pairedSinkAddr_.empty()) {
                                /* paired sink addr should not be empty: notify with error  */
                                YIO_LOG_ERROR_EVENT("LinkplayA98BluetoothImpl.PairedDevoceAddrEmpty", "Paired Device addr is empty!");
                                this_->sourceEventCallbackLocked(Bluetooth::SourceEvent::PAIRED, result);
                                break;
                            }
                            string_to_bt_address(this_->pairedSinkAddr_.c_str(), &remoteBdAddr);
                        } catch (const Json::Exception& e) {
                            YIO_LOG_ERROR_EVENT("LinkplayA98BluetoothImpl.SourceEvent.InvalidJson", "Can't parse bluetooth msg json: " << msg->msg_value);
                            this_->sourceEventCallbackLocked(Bluetooth::SourceEvent::PAIRED, result);
                            break;
                        }
                        if (linkplay_bluetooth_get_remote_device_by_addr(&remoteDeviceInfo,
                                                                         remoteBdAddr) == kBtErrorOk) {
                            /* Successfully Connected: Since we PAIR -> need to start stream,
                             * when BT_SOURCE_AV_START_EVT will occur -> send PAIRED success
                             */
                            this_->startStream();
                        } else {
                            YIO_LOG_ERROR_EVENT("LinkplayA98BluetoothImpl.SourceEvent.GetDeviceFailed", "Can't get remote device by addr: " << this_->pairedSinkAddr_);
                            this_->sourceEventCallbackLocked(Bluetooth::SourceEvent::PAIRED, result);
                        }
                    } else {
                        /* TODO: Check error type */
                        YIO_LOG_ERROR_EVENT("LinkplayA98BluetoothImpl.Source.ConnectionError", "Bluetooth Source Connection Event Error");
                        this_->sourceEventCallbackLocked(Bluetooth::SourceEvent::PAIRED, result);
                    }
                    break;
                }
                case BT_SOURCE_DISCONNECTED_EVT: {
                    if (!this_->sourceProfileEnabled_) {
                        break;
                    }
                    this_->pairedSinkAddr_.clear();
                    linkplay_bluetooth_stop_stream();
                    set_bt_transfer(0); /* turn off alsa lib transmit pcm */
                    /* FIXME: It's not actually disconnect all. Need to keep "currently connected
                     * networks" and check that list is empty -> then send disconnect_all */
                    result.result = Bluetooth::EventResult::Result::OK;
                    this_->baseEventCallbackLocked(Bluetooth::BaseEvent::DISCONNECT_ALL, result);
                    this_->sourceEventCallbackLocked(Bluetooth::SourceEvent::DISCONNECTED, result);
                    break;
                }
                case BT_SOURCE_CFG_RSP_EVT: {
                    /* Sink device is master -> need rework resampling feature here! */
                    std::string evtMsg = msg->msg_value != nullptr ? msg->msg_value : "";
                    YIO_LOG_INFO("BT SOURCE CFG RSP EVT: " << evtMsg);
                    break;
                }
                case BT_SOURCE_AV_START_EVT: {
                    /* Device connected And streaming started */
                    result.network.addr = this_->pairedSinkAddr_;
                    result.network.role = BtRole::SINK;
                    /* TODO: set up name */
                    result.result = Bluetooth::EventResult::Result::OK;
                    this_->sourceEventCallbackLocked(Bluetooth::SourceEvent::PAIRED, result);
                    break;
                }
                case BT_SOURCE_AV_STOP_EVT: {
                    /* Stream Stopped  */
                }
                default: {
                    /* TODO handle it*/
                    break;
                }
            }

            break;
        }
        default: {
            YIO_LOG_DEBUG("Another event: ");
        }
    }
    return 0;
}

int LinkplayA98BluetoothImpl::dispatchCommand()
{
    std::lock_guard<std::mutex> guard(commandMutex_);
    while (!commandsQueue_.empty()) {
        BtError err = kBtErrorOk;
        const auto& cmd = commandsQueue_.front();
        // avoid spam of SET_VOLUME_ABS command
        if (cmd.cmd != lastCmd_ || cmd.cmd != COMMAND_SET_VOLUME_ABS) {
            YIO_LOG_DEBUG("Dispatch bt command " << cmd.cmd);
            lastCmd_ = cmd.cmd;
        }
        switch (cmd.cmd) {
            case COMMAND_START_SERVICES: {
                /* Start Linkplay BT internal services*/
                err = linkplay_bluetooth_start_services(cmd.params[0]);
                break;
            }
            case COMMAND_SET_VISIBILITY: {
                err = linkplay_bluetooth_set_visibility(cmd.params[0], cmd.params[1]);

                Bluetooth::SinkEvent ev = Bluetooth::SinkEvent::NON_VISIBLE;
                Bluetooth::EventResult result;
                if (cmd.params[0] != 0 && cmd.params[1] != 0) {
                    ev = Bluetooth::SinkEvent::DISCOVERABLE_CONNECTABLE;
                } else if (cmd.params[0] != 0 && cmd.params[1] == 0) {
                    ev = Bluetooth::SinkEvent::DISCOVERABLE;
                } else if (cmd.params[0] == 0 && cmd.params[1] != 0) {
                    ev = Bluetooth::SinkEvent::CONNECTABLE;
                }
                if (err != kBtErrorOk) {
                    result.result = Bluetooth::EventResult::Result::ERROR;
                } else {
                    result.result = Bluetooth::EventResult::Result::OK;
                }
                sinkEventCallbackLocked(ev, result);
                break;
            }
            case COMMAND_START_SCAN: {
                /*
                 * link..._start_scan function makes N scan cycles (1.28 sec each), where
                 * N is an amount of these cycles.
                 */
                static constexpr int SCAN_DURATION_CYCLE_AMOUNT = 4;
                err = linkplay_bluetooth_start_scan(SCAN_DURATION_CYCLE_AMOUNT);
                break;
            }
            case COMMAND_CANCEL_SCAN: {
                err = linkplay_bluetooth_cancel_scan();
                /* TODO: Check that scan is canceled and send SCAN_CANCELED message */
                break;
            }
            case COMMAND_SOURCE_CONNECT: {
                if (!sourceProfileEnabled_) {
                    break;
                }
                BD_ADDR addr;
                string_to_bt_address(cmd.btAddress.c_str(), &addr);
                err = linkplay_bluetooth_connect_by_addr(BT_ROLE_SOURCE, &addr, 3000);
                switch (err) {
                    case kBtConnectFailed:
                    case KBtAlreadyConnecting:
                    case kBtErrorFailed: {
                        Bluetooth::EventResult result;
                        result.result = Bluetooth::EventResult::Result::ERROR;
                        /* Notify User that we can't connect */
                        /* TODO: need fill up network field */
                        sourceEventCallbackLocked(Bluetooth::SourceEvent::PAIRED, result);
                        break;
                    }
                    case kBtHistoryNotFound:
                    case kBtDevicesNoHistory: {
                        /* For some reasons kBtHistoryNotFound is return when can't connect to device
                         * It is wrong error -> FIXME: Request changes in Linkplay
                         */
                        Bluetooth::EventResult result;
                        result.result = Bluetooth::EventResult::Result::ERROR;
                        /* Notify User that we can't connect */
                        /* TODO: need fill up network field */
                        sourceEventCallbackLocked(Bluetooth::SourceEvent::PAIRED, result);
                        break;
                    }
                    case kBtErrorOk: {
                        /* it's okay. wait for Event */
                        break;
                    }
                    default: {
                        /* Unexpected behavior*/
                        break;
                    }
                }
                break;
            }
            case COMMAND_DISCONNECT_FROM:
                /* FIXME: Currently we disconnect all. Need disconnect from target*/
            case COMMAND_DISCONNECT_ALL: {
                /* NOTE: As a parameter we set up LOCAL ROLE, so if want to disconnect connected SINKs setup source as a
                 * parameter */
                /* Disconnect source devices */
                if (cmd.roleToDisconnect == BtRole::SOURCE || cmd.roleToDisconnect == BtRole::ALL) {
                    linkplay_bluetooth_play_stop_all(BT_ROLE_SINK);
                    linkplay_bluetooth_close_all(BT_ROLE_SINK);
                }
                /* Do not call for Source Profile methods if it wasn't inited. It will cause segfault */
                if (sourceProfileEnabled_) {
                    /* Disconnect sink devices */
                    if (cmd.roleToDisconnect == BtRole::SINK || cmd.roleToDisconnect == BtRole::ALL) {
                        linkplay_bluetooth_play_stop_all(BT_ROLE_SOURCE);
                        linkplay_bluetooth_close_all(BT_ROLE_SOURCE);
                    }
                }
                /*TODO: If 0 connections -> send OK message */
                break;
            }
            case COMMAND_SET_VOLUME_ABS: {
                for (const auto& i : curSourceConnMacs_) {
                    BD_ADDR addr;
                    string_to_bt_address(i.c_str(), &addr);
                    int connIdx = linkplay_bluetooth_get_connection_index_by_addr(addr);
                    if (connIdx < 0) {
                        YIO_LOG_ERROR_EVENT("LinkplayA98BluetoothImpl.GetConnectionFailed", "Can't get connection Index");
                        continue;
                    }
                    int ret = linkplay_bluetooth_set_abs_volume(BT_ROLE_SINK, cmd.volume, connIdx);
                    Bluetooth::EventResult result;
                    result.avrcpEvent = AVRCP::CHANGE_VOLUME_ABS;
                    result.result = Bluetooth::EventResult::Result::OK;
                    if (ret != 0) {
                        YIO_LOG_ERROR_EVENT("LinkplayA98BluetoothImpl.SetAbsVolumeFailed", "Set volume abs error (can't send AVRCP event)");
                        result.result = Bluetooth::EventResult::Result::ERROR;
                    }
                    /* Send result of avrcp event send */
                    sinkEventCallbackLocked(SinkEvent::AVRCP_OUT, result);
                }
                break;
            }
            case COMMAND_PLAYBACK_PREV: {
                /*FIXME: Handle real connection ids */
                err = linkplay_bluetooth_playback_prev(BT_ROLE_SINK, 0);
                break;
            }
            case COMMAND_PLAYBACK_NEXT: {
                err = linkplay_bluetooth_playback_next(BT_ROLE_SINK, 0);
                break;
            }

            case COMMAND_PLAYBACK_START: {
                /* TODO: Source device should support it. Add to Bluetooth API? */
                err = linkplay_bluetooth_playback_resume(cmd.params[0], 0);
                break;
            }
            case COMMAND_PLAYBACK_PAUSE: {
                err = linkplay_bluetooth_playback_pause(cmd.params[0], 0);
                break;
            }

            case COMMAND_START_STREAM: {
                tLINKPLAY_AV_MEDIA_FEED_CFG_PCM pcmConf = streamSender_.getPcmConfig();
                err = linkplay_bluetooth_source_start_stream(&pcmConf);
                set_bt_transfer(1); /* FIXME: update comment: Notify ALSA to write into fifo*/
                if (err != kBtErrorOk) {
                    Bluetooth::EventResult result;
                    result.result = Bluetooth::EventResult::Result::ERROR;
                    sourceEventCallbackLocked(Bluetooth::SourceEvent::PAIRED, result);
                }
                break;
            }
            case COMMAND_STOP_STREAM: {
                if (!sourceProfileEnabled_) {
                    break;
                }
                err = linkplay_bluetooth_stop_stream();
                set_bt_transfer(0); /* Notify ALSA to stop writing into fifo */
                break;
            }
            default: {
                break;
            }
        }
        commandsQueue_.pop();
    }
    return 0;
}

void LinkplayA98BluetoothImpl::bluetoothThread()
{
    int timeoutMs = -1;
    while (threadExists_) {
        /* This is Linkplay internal Condition Variable analogue. We can't get any event without
         * this function call
         */
        auto err = linkplay_bluetooth_event_dump(timeoutMs);

        if (err == kBtErrorInitFailed) {
            YIO_LOG_ERROR_EVENT("LinkplayA98BluetoothImpl.InitFailed", "Server Init Error: " << btErrorStr(err));
            throw std::runtime_error("Server Init Error: " + btErrorStr(err));
        } else if (err == KBtErrorInitProcessing) {
            timeoutMs = 50;
            continue;
        } else if (err == kBtErrorOk) {
            timeoutMs = -1;
        }

        dispatchCommand();
    }
}

void LinkplayA98BluetoothImpl::start()
{
    /* Set up debug level; TODO: Set up to 0 at release */
    linkplay_bluetooth_set_debug_level(1 << 3);

    memset(btConfig_.bt_name, 0x00, BD_NAME_LEN + 1);
    strncpy(reinterpret_cast<char*>(btConfig_.bt_name), name_.c_str(),
            (name_.size() < BD_NAME_LEN) ? name_.size() : BD_NAME_LEN);
    YIO_LOG_INFO("Init bluetooth server with name: " << btConfig_.bt_name);
    btConfig_.bas_server_init_call_back = LinkplayA98BluetoothImpl::setBsaServerMode;
    btConfig_.support_aac_decode = 0;
    BtError err = linkplay_bt_server_init(&btConfig_);

    if (err != kBtErrorOk) {
        YIO_LOG_ERROR_EVENT("LinkplayA98BluetoothImpl.InitBsaServerFailed", "Can't init BSA (bluetooth) Server");
        throw std::runtime_error("Can't Init BSA (bluetooth) server");
    }

    err = linkplay_bluetooth_registerEventCallbacks(eventCallback, this);
    if (err != kBtErrorOk) {
        YIO_LOG_ERROR_EVENT("LinkplayA98BluetoothImpl.RegiterCallbacksFailed", "Can't register event callbacks for BSA (bluetooth) server");
        throw std::runtime_error("Can't register event callbacks for BSA (bluetooth) server");
    }

    memset(&streamInCallbacks_, 0x00, sizeof(streamInCallbacks_));
    streamInCallbacks_.pcm_open_call_fn = LinkplayA98BluetoothImpl::open;
    streamInCallbacks_.pcm_close_call_fn = LinkplayA98BluetoothImpl::close;
    streamInCallbacks_.pcm_write_data_fn = LinkplayA98BluetoothImpl::writeData;

    linkplay_bluetooth_registerAudioRenderCallBack(streamInCallbacks_, this);

    btThread_ = std::thread(std::bind(&LinkplayA98BluetoothImpl::bluetoothThread, this));

    /* According to Linkplay API header -> run sink before source "service" */
    perform(BtAppCommands::START_SINK);
    /* Make IRBIS/Elari connectable at device boot up */
    setVisibility(false /* discoverable */, true /* connectable */);
}

void* LinkplayA98BluetoothImpl::open(void* context, UINT8 format, UINT16 sampleRate, UINT8 numChannel, UINT8 bitPerSample)
{
    LinkplayA98BluetoothImpl* this_ = reinterpret_cast<LinkplayA98BluetoothImpl*>(context);
    /* Open Default alsa device */
    this_->streamAlsaWriter_.open("default", numChannel, static_cast<snd_pcm_format_t>(format), sampleRate, bitPerSample);
    /* Return not NULL value, so Linkplay bt will call writeData callback */
    return this_;
}

void LinkplayA98BluetoothImpl::close(void* /* context */, void* /* pcm_handle */)
{
    YIO_LOG_INFO("Bt stream close");
}

int LinkplayA98BluetoothImpl::writeData(void* context, void* /* pcm_handle */, int /* connection */, UINT8* pbuffer, int length) noexcept {
    LinkplayA98BluetoothImpl* this_ = reinterpret_cast<LinkplayA98BluetoothImpl*>(context);
    return this_->handleWriteData(pbuffer, length);
}

int LinkplayA98BluetoothImpl::handleWriteData(UINT8* pbuffer, int length)
{
    handleStartPlaying();
    /* copy data */
    std::vector<char> data(length);
    memcpy(data.data(), pbuffer, length);
    streamAlsaWriter_.write(std::move(data), focusCoef_.load());
    /* Timer will put device into "not playing" state after timeout and notify user */
    constexpr std::chrono::milliseconds NO_STREAM_TIMEOUT = std::chrono::milliseconds(350);
    playingTimer_->start(NO_STREAM_TIMEOUT);
    return length;
}

int LinkplayA98BluetoothImpl::perform(LinkplayA98BluetoothImpl::BtAppCommands command)
{
    return perform(command, false, false);
}

int LinkplayA98BluetoothImpl::perform(LinkplayA98BluetoothImpl::BtAppCommands command, int volume)
{
    volumeToSet_ = volume;
    return perform(command, false, false);
}

int LinkplayA98BluetoothImpl::perform(BtAppCommands command, bool discoverable, bool connectable)
{
    // avoid spam log
    if (command != BtAppCommands::SET_VOLUME_ABS) {
        YIO_LOG_DEBUG("Perform " << (int)command);
    }
    std::lock_guard<std::mutex> guard(commandMutex_);
    BtCommand btCommand;
    switch (command) {
        case BtAppCommands::START_SINK: {
            btCommand.cmd = COMMAND_START_SERVICES;
            btCommand.params.push_back(BT_A2DP_SINK);
            break;
        }
        case BtAppCommands::START_SOURCE: {
            btCommand.cmd = COMMAND_START_SERVICES;
            btCommand.params.push_back(BT_A2DP_SOURCE);
            break;
        }
        case BtAppCommands::SET_VISIBILITY: {
            btCommand.cmd = COMMAND_SET_VISIBILITY;
            btCommand.params.push_back(uint32_t(discoverable));
            btCommand.params.push_back(uint32_t(connectable));
            break;
        }
        case BtAppCommands::START_SCAN: {
            btCommand.cmd = COMMAND_START_SCAN;
            break;
        }
        case BtAppCommands::CANCEL_SCAN: {
            btCommand.cmd = COMMAND_CANCEL_SCAN;
            break;
        }
        case BtAppCommands::SOURCE_CONNECT: {
            btCommand.cmd = COMMAND_SOURCE_CONNECT;
            btCommand.btAddress = performConnectionAddr_;
            break;
        }
        case BtAppCommands::DISCONNECT_FROM: {
            /* FIXME: Add target */
            btCommand.cmd = COMMAND_DISCONNECT_FROM;
            btCommand.params.push_back(performRole_ == Bluetooth::BtRole::SINK ? BT_ROLE_SINK
                                                                               : BT_ROLE_SOURCE);
            break;
        }
        case BtAppCommands::PLAYBACK_START: {
            btCommand.params.push_back(performRole_ == Bluetooth::BtRole::SINK ? BT_ROLE_SINK
                                                                               : BT_ROLE_SOURCE);
            btCommand.cmd = COMMAND_PLAYBACK_START;
            break;
        }
        case BtAppCommands::PLAYBACK_PAUSE: {
            btCommand.params.push_back(performRole_ == Bluetooth::BtRole::SINK ? BT_ROLE_SINK
                                                                               : BT_ROLE_SOURCE);
            btCommand.cmd = COMMAND_PLAYBACK_PAUSE;
            break;
        }
        case BtAppCommands::PLAYBACK_PREV: {
            btCommand.cmd = COMMAND_PLAYBACK_PREV;
            break;
        }
        case BtAppCommands::PLAYBACK_NEXT: {
            btCommand.cmd = COMMAND_PLAYBACK_NEXT;
            break;
        }
        case BtAppCommands::START_STREAM: {
            btCommand.cmd = COMMAND_START_STREAM;
            break;
        }
        case BtAppCommands::STOP_STREAM: {
            btCommand.cmd = COMMAND_STOP_STREAM;
            break;
        }
        case BtAppCommands::SET_VOLUME_ABS: {
            btCommand.cmd = COMMAND_SET_VOLUME_ABS;
            btCommand.volume = volumeToSet_;
            break;
        }
        default: {
            break;
        }
    } /* switch */
    commandsQueue_.push(btCommand);

    /* Notify Bluetooth Thread about event */
    linkplay_bluetooth_schedule_event();

    return 0;
}

int LinkplayA98BluetoothImpl::perform(BtAppCommands command, const std::string& btAddr)
{
    if (command != BtAppCommands::SOURCE_CONNECT) {
    }
    performConnectionAddr_ = btAddr;
    return perform(command, false, false);
}

int LinkplayA98BluetoothImpl::perform(LinkplayA98BluetoothImpl::BtAppCommands commands, Bluetooth::BtRole role)
{
    performRole_ = role;
    return perform(commands, false, false);
}

bool LinkplayA98BluetoothImpl::isAsSinkPlaying() const {
    return isPlaying_.load();
}

void LinkplayA98BluetoothImpl::playingTimeout() {
    isPlaying_.store(false);
    /* Stoped playing a2dp stream. notfy user */
    Bluetooth::EventResult result;
    result.result = Bluetooth::EventResult::Result::OK;
    result.avrcpEvent = AVRCP::PLAY_STOP;
    sinkEventCallbackLocked(Bluetooth::SinkEvent::AVRCP_IN, result);
}

void LinkplayA98BluetoothImpl::handleStartPlaying() {
    if (!isPlaying_.exchange(true)) {
        /* Started playing stream. notify user */
        Bluetooth::EventResult result;
        result.result = Bluetooth::EventResult::Result::OK;
        result.avrcpEvent = AVRCP::PLAY_START;
        sinkEventCallbackLocked(Bluetooth::SinkEvent::AVRCP_IN, result);
    }
}

void LinkplayA98BluetoothImpl::takeAudioFocus() {
    focusCoef_.store(1.0);
}

void LinkplayA98BluetoothImpl::freeAudioFocus() {
    focusCoef_.store(0.1);
}
