#include "mt8516_bluetooth_impl.h"

#include <smart_devices/third_party/mediatek/mt8516/mtk_bt_service_a2dp_wrapper.h>
#include <smart_devices/third_party/mediatek/mt8516/mtk_bt_service_avrcp_wrapper.h>

#include <yandex_io/libs/base/persistent_file.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <cstring>
#include <stdexcept>

YIO_DEFINE_LOG_MODULE("bluetooth");

using namespace quasar;

MT8516BluetoothImpl::MT8516BluetoothImpl(const std::string& name, std::string currentConnectionCacheFile)
    : Bluetooth(name)
    , currentConnectionCacheFile_(std::move(currentConnectionCacheFile))
{
    /* In case if process crashed need to restore information about current connection (addr). Otherwise device won't
     * be able to control bt (send avrcp or disconnect from current network)
     */
    restoreCurrentNetwork();

    /* Start MTK bluetooth init sequence */
    init();
}

void MT8516BluetoothImpl::init()
{
    a_mtk_bt_service_init();

    mtkCallbacks_.bt_event_cb = &MT8516BluetoothImpl::mtkrpcapi_BtAppGapEventCbk;
    mtkCallbacks_.bt_get_pairing_key_cb = &MT8516BluetoothImpl::mtkrpcapi_BtAppGapGetPairingKeyCbk;
    mtkCallbacks_.bt_dev_info_cb = &MT8516BluetoothImpl::mtkrpcapi_BtAppGapInquiryResponseCbk;
    int ret = a_mtkapi_gap_bt_base_init(&mtkCallbacks_, this);
    if (ret != 0) {
        throw std::runtime_error("Can't init bluetooth client service");
    }

    /* Register callback for A2DP stream events (play/pause, etc..) */
    a_mtkapi_a2dp_register_callback(&MT8516BluetoothImpl::mtkrpcapi_BtAppA2dpCbk, this);
    /* Register callback for AVRCP events (volume, etc..) */
    a_mtkapi_avrcp_register_callback(&MT8516BluetoothImpl::mtkrpcapi_BtAppAvrcpCbk, this);

    /* Enable SINK profile for local device. MT bt support only one active profile */
    ret = a_mtkapi_a2dp_sink_enable(TRUE);
    if (ret != 0) {
        throw std::runtime_error("Can't enable bluetooth as SINK");
    }

    ret = a_mtkapi_bt_gap_on_off(TRUE);
    if (ret != 0) {
        throw std::runtime_error("Can't Power ON bluetooth");
    }
    /* Set up default Bluetooth name for device. */
    applyNewName(name_);

    std::lock_guard<std::mutex> guard(fadeMutex_);
    setFadeSettings(FadeSettings::DEFAULT);
    /* Check restored isPlaying_ value. Do not mute current stream if it is already playing */
    if (!isPlaying_) {
        /* Mute stream by default */
        a_mtkapi_a2dp_sound_mute(TRUE);
    }

    std::unique_lock<std::mutex> lock(powerStateMutex_);
    powerStateCondVar_.wait_for(lock, std::chrono::seconds(5), [this]() {
        return isPoweredOn_;
    });
    YIO_LOG_INFO("Bluetooth is " << (isPoweredOn_ ? "enabled" : "not enabled yet") << ". Set default Visibility: Connectable, Not Discoverable");
    const BOOL isDisc = FALSE;
    const BOOL isConn = TRUE;
    ret = a_mtkapi_bt_gap_set_connectable_and_discoverable(isConn, isDisc);
    YIO_LOG_DEBUG("Set Default Bluetooth Visibility: " << ret);
}

void MT8516BluetoothImpl::setFadeSettings(FadeSettings type)
{
    int maxVolume = 0;
    int minVolume = 0;
    int fadeInStep = 0;
    int fadeInFrameOnEachStep = 0;
    int fadeOutStep = 0;
    int fadeOutFrameOnEachStep = 0;
    switch (type) {
        case FadeSettings::DEFAULT: {
            /* Set up smooth FadeIn but quick FadeOut */
            maxVolume = MAX_VOLUME;      /* Actual volume BT stream volume */
            minVolume = MIN_VOLUME;      /* zero volume */
            fadeInStep = 16;             /* there will be (maxVolume / fadeInStep) FadeIn steps */
            fadeInFrameOnEachStep = 75;  /* Play 75 frames on each FadeIn volume step */
            fadeOutStep = 32;            /* there will be (maxVolume / fadeOutStep) FadeOut steps */
            fadeOutFrameOnEachStep = 10; /* Play 10 frames on each FadeOut volume step */
            break;
        }
        case FadeSettings::MUSIC_FOCUS: {
            /* Music is already playing. Set min volume as FOCUS_VOLUME */
            maxVolume = MAX_VOLUME;
            minVolume = FOCUS_VOLUME;
            /* set up the fade step about 60 (volume units) for Focus FadeIn (to return focus quite fast, but smoothly) */
            fadeInStep = (maxVolume - minVolume) / 1024;
            /* set up fade step 230 (volume units) for Focus FadeOut (to get focus quite fast) */
            fadeOutStep = (maxVolume - minVolume) / 256;
            fadeInFrameOnEachStep = fadeOutFrameOnEachStep = 50;
            break;
        }
        case FadeSettings::UPDATE_MUSIC_IN_FOCUS: {
            /* Music starting to play when Focus is already captured by someone. */
            maxVolume = FOCUS_VOLUME;
            minVolume = MIN_VOLUME;
            /* Since volume in Focus is not very high make quite Smooth FadeIn/Out */
            fadeInStep = fadeOutStep = 16;
            fadeInFrameOnEachStep = fadeOutFrameOnEachStep = 75;
            break;
        }
    }
    a_mtkapi_a2dp_sound_set_fade(maxVolume, minVolume, fadeOutStep, fadeOutFrameOnEachStep, fadeInStep, fadeInFrameOnEachStep);
}

MT8516BluetoothImpl::~MT8516BluetoothImpl()
{
    a_mtkapi_bt_gap_on_off(FALSE);
    std::unique_lock<std::mutex> lock(powerStateMutex_);
    powerStateCondVar_.wait_for(lock, std::chrono::seconds(5), [this]() {
        return isPoweredOn_ == false;
    });
    lock.unlock();
    startStreamWithoutAvrcpExecutor_.reset();
    factoryResetExecutor_.reset();
}

int MT8516BluetoothImpl::setName(const std::string& name)
{
    if (name == name_) {
        /* name didn't change. no need to apply it */
        return 0;
    }
    const int ret = applyNewName(name);
    if (ret == 0) {
        name_ = name;
    }
    return ret;
}

int MT8516BluetoothImpl::applyNewName(const std::string& name) {
    YIO_LOG_INFO("Set new bluetooth name: " << name);
    return a_mtkapi_bt_gap_set_name((char*)name.c_str());
}

void MT8516BluetoothImpl::freeAudioFocus()
{
    std::lock_guard<std::mutex> guard(fadeMutex_);
    freeFocus_ = true;
    YIO_LOG_DEBUG("Bluetooth: Free audio focus: " << isPlaying_);
    if (!isPlaying_) {
        /* do not fade when nothing is playing */
        return;
    }
    setFadeSettings(FadeSettings::MUSIC_FOCUS);
    a_mtkapi_a2dp_sound_mute(TRUE);
}

void MT8516BluetoothImpl::takeAudioFocus()
{
    std::lock_guard<std::mutex> guard(fadeMutex_);
    freeFocus_ = false;
    YIO_LOG_DEBUG("Bluetooth: Take audio focus: " << isPlaying_);
    if (!isPlaying_) {
        /* do not fade when nothing is playing */
        return;
    }
    setFadeSettings(FadeSettings::MUSIC_FOCUS);

    a_mtkapi_a2dp_sound_mute(FALSE);
}

int MT8516BluetoothImpl::setVisibility(bool isDiscoverable, bool isConnectable)
{
    BOOL isDisc = isDiscoverable ? TRUE : FALSE;
    BOOL isConn = isConnectable ? TRUE : FALSE;
    int ret = a_mtkapi_bt_gap_set_connectable_and_discoverable(isConn, isDisc);
    YIO_LOG_INFO("Set Bluetooth Visibility: " << ret);
    if (!ret) {
        auto ev = SinkEvent::NON_VISIBLE;
        if (isDiscoverable && isConnectable) {
            ev = SinkEvent::DISCOVERABLE_CONNECTABLE;
        } else if (isDiscoverable) {
            ev = SinkEvent::DISCOVERABLE;
        } else if (isConnectable) {
            ev = SinkEvent::CONNECTABLE;
        }
        sinkEventCallbackLocked(ev, EventResult{});
    }
    return ret;
}

int MT8516BluetoothImpl::disconnectAll(BtRole /* role */)
{
    std::lock_guard<std::mutex> guard(mutex_);
    CHAR addr[MAX_NAME_LEN]{};
    for (const auto& [btAddr, device] : connectedDevices_) {
        YIO_LOG_INFO("Disconnect from: " << btAddr);
        ::strcpy(addr, btAddr.c_str());
        a_mtkapi_a2dp_disconnect(addr);
    }
    /* @TODO: Check event */
    return 0;
}

int MT8516BluetoothImpl::scanNetworks()
{
    std::unique_lock<std::mutex> lock(mutex_);
    int ret = a_mtkapi_bt_gap_start_inquiry_scan(BT_INQUIRY_FILTER_TYPE_A2DP_SNK);
    if (ret != 0) {
        Bluetooth::EventResult result;
        result.result = Bluetooth::EventResult::Result::ERROR;
        baseEventCallbackLocked(Bluetooth::BaseEvent::SCANNED, result);
        return -1;
    }
    lock.unlock();

    /* fixme: rework it */
    /* Sleep for 6 seconds, so networks_ will be field in callbacks */
    std::this_thread::sleep_for(std::chrono::seconds(6));

    Bluetooth::EventResult result;
    result.result = Bluetooth::EventResult::Result::OK;
    result.scanResult = networks_;
    baseEventCallbackLocked(Bluetooth::BaseEvent::SCANNED, result);
    return 0;
}

int MT8516BluetoothImpl::pairWithSink(const Bluetooth::BtNetwork& /* network */)
{
    return 0;
}

int MT8516BluetoothImpl::asSinkPlayNext(const Bluetooth::BtNetwork& /* network */)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (activeSource_.empty()) {
        return -1;
    }
    return a_mtkapi_avrcp_send_passthrough_cmd((char*)activeSource_.c_str(), BT_AVRCP_CMD_TYPE_FWD, BT_AVRCP_KEY_STATE_AUTO);
}

int MT8516BluetoothImpl::asSinkPlayPrev(const Bluetooth::BtNetwork& /* network */)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (activeSource_.empty()) {
        return -1;
    }
    return a_mtkapi_avrcp_send_passthrough_cmd((char*)activeSource_.c_str(), BT_AVRCP_CMD_TYPE_BWD, BT_AVRCP_KEY_STATE_AUTO);
}

int MT8516BluetoothImpl::asSinkPlayPause(const Bluetooth::BtNetwork& /* network */)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (activeSource_.empty()) {
        return -1;
    }
    return a_mtkapi_avrcp_send_passthrough_cmd((char*)activeSource_.c_str(), BT_AVRCP_CMD_TYPE_PAUSE, BT_AVRCP_KEY_STATE_AUTO);
}

int MT8516BluetoothImpl::asSinkPlayStart(const Bluetooth::BtNetwork& /* network */)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (activeSource_.empty()) {
        return -1;
    }
    return a_mtkapi_avrcp_send_passthrough_cmd((char*)activeSource_.c_str(), BT_AVRCP_CMD_TYPE_PLAY, BT_AVRCP_KEY_STATE_AUTO);
}

/* Volume is BT AbsVolume. Value in [0..127] */
int MT8516BluetoothImpl::asSinkSetVolumeAbs(int volume)
{
    /* Send volume in 0..100 via avrcp */
    const UINT8 volumePercent = (volume / 127.0) * 100;
    a_mtkapi_avrcp_update_absolute_volume(volumePercent);
    return 0;
}

void MT8516BluetoothImpl::mtkrpcapi_BtAppA2dpCbk(BT_A2DP_EVENT_PARAM* param, void* pv_tag)
{
    if (!param || !pv_tag) {
        return;
    }
    MT8516BluetoothImpl* this_ = reinterpret_cast<MT8516BluetoothImpl*>(pv_tag);
    this_->a2dpCallback(param);
}

void MT8516BluetoothImpl::a2dpCallback(BT_A2DP_EVENT_PARAM* param)
{
    YIO_LOG_INFO("A2DP callback. Param: " << param->event);
    if (param->event == BT_A2DP_EVENT_STREAM_START) {
        /* Schedule a callback that will enable stream if Source device doesn't support AVRCP Start/Stop callbacks */
        auto playStreamLambda = [this]() {
            streamStartedWithoutAvrcp_ = true;
            handlePlayerStatusChange(true);
            YIO_LOG_INFO("Start playing stream without AVRCP");
        };
        constexpr auto executorType = PeriodicExecutor::PeriodicType::ONE_SHOT;
        static const auto startStreamTimeoutMs = std::chrono::milliseconds(2000);
        std::lock_guard<std::mutex> guard(periodicMutex_);
        startStreamWithoutAvrcpExecutor_ = std::make_unique<PeriodicExecutor>(playStreamLambda, startStreamTimeoutMs, executorType);
    } else if (param->event == BT_A2DP_EVENT_STREAM_SUSPEND) {
        {
            /* Destroy callback (avoid extra callback or just clean resources) */
            std::lock_guard<std::mutex> guard(periodicMutex_);
            startStreamWithoutAvrcpExecutor_.reset(nullptr);
        }
        if (streamStartedWithoutAvrcp_.exchange(false)) {
            /* AVRCP START/STOP didn't interrupt scheduled callback. Handle Actual stream stop as AVRCP (control fade,
             * send proper callbacks to listners)
             */
            handlePlayerStatusChange(false);
            YIO_LOG_INFO("Stop playing stream without AVRCP");
        }
    } else if (param->event == BT_A2DP_EVENT_PLAYER_EVENT) {
        YIO_LOG_INFO("A2DP Player event: " << param->data.player_event);
    } else if (param->event == BT_A2DP_EVENT_CONNECTED) {
        /* Notify BT Stack that it's time to start play steam to alsa */
        a_mtkapi_a2dp_sink_start_player();
    } else if (param->event == BT_A2DP_EVENT_DISCONNECTED) {
        a_mtkapi_a2dp_sink_stop_player();
    }
}

void MT8516BluetoothImpl::mtkrpcapi_BtAppAvrcpCbk(BT_AVRCP_EVENT_PARAM* param, VOID* pv_tag) {
    if (!param || !pv_tag) {
        return;
    }
    MT8516BluetoothImpl* this_ = reinterpret_cast<MT8516BluetoothImpl*>(pv_tag);
    this_->avrcpCallback(param);
}

void MT8516BluetoothImpl::avrcpCallback(BT_AVRCP_EVENT_PARAM* param)
{
    YIO_LOG_INFO("AVRCP callback. Param: " << param->event << " Addr: " << param->addr);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (param->addr != activeSource_) {
            activeSource_ = param->addr;
        }
    }
    switch (param->event) {
        case BT_AVRCP_EVENT_SET_VOLUME_REQ: {
            const int volumePercent = param->data.volume_change.abs_volume;
            Bluetooth::EventResult result;
            result.result = Bluetooth::EventResult::Result::OK;
            result.avrcpEvent = AVRCP::CHANGE_VOLUME_ABS;
            /* Received volume in [0..100]. Need to send it in [0..127] */
            result.volumeAbs = volumePercent / 100.0 * 127;
            YIO_LOG_DEBUG("Avrcp volume change. Percent: "
                          << int(param->data.volume_change.abs_volume) << "Abs: " << result.volumeAbs);
            sinkEventCallbackLocked(Bluetooth::SinkEvent::AVRCP_IN, result);
            break;
        }
        case BT_AVRCP_EVENT_PLAY_STATUS_CHANGE: {
            /* Remove scheduled callback, so it won't duplicate this callback */
            streamStartedWithoutAvrcp_ = false;
            /* fixme: Race condition still may occur. A2DP and AVRCP callbacks may come in random order */
            {
                std::lock_guard<std::mutex> guard(periodicMutex_);
                startStreamWithoutAvrcpExecutor_.reset(nullptr);
            }
            /* Handle AVRCP start/stop events */
            const auto playStatus = param->data.play_status_change.play_status;
            if (playStatus == AVRCP_PLAY_STATUS_PAUSED || playStatus == AVRCP_PLAY_STATUS_STOPPED) {
                handlePlayerStatusChange(false);
            } else if (playStatus == AVRCP_PLAY_STATUS_PLAYING) {
                handlePlayerStatusChange(true);
            }
            YIO_LOG_DEBUG("AVRCP: Play Status event: " << playStatus);
            break;
        }
        case BT_AVRCP_EVENT_TRACK_CHANGE: {
            const auto& meta = param->data.track_change.element_attr;
            handleTrackMeta(meta);
            break;
        }
        case BT_AVRCP_EVENT_POS_CHANGE: {
            const auto& position = param->data.pos_change;
            YIO_LOG_INFO("AVRCP Position Changed: Curr: " << position.song_pos << "Ms. Len: " << position.song_len << " ms");
            break;
        }
        default: {
            YIO_LOG_TRACE("Unhandled AVRCP event: " << param->event);
            break;
        }
    }
}

void MT8516BluetoothImpl::handlePlayerStatusChange(bool isPlaying)
{
    std::unique_lock<std::mutex> lock(fadeMutex_);
    isPlaying_ = isPlaying;
    if (isPlaying) {
        /* Start FadeIn */
        if (!freeFocus_) {
            setFadeSettings(FadeSettings::DEFAULT);
        } else {
            /* Music is starting to play, but someone captured focus. FadeIn with FOCUS_VOLUME as max volume */
            setFadeSettings(FadeSettings::UPDATE_MUSIC_IN_FOCUS);
        }
        a_mtkapi_a2dp_sound_mute(FALSE);

        /* Pass AVRCP event through to Bluetooth listeners */
        EventResult event;
        event.avrcpEvent = AVRCP::PLAY_START;
        event.result = EventResult::Result::OK;
        sinkEventCallbackLocked(SinkEvent::AVRCP_IN, event);

    } else {
        /* Mute future stream. FadeOut work here, but when this event received stream is already stopped actually,
         * but it is necessary to call it, so future Stream will start from 0 volume
         */
        if (!freeFocus_) {
            setFadeSettings(FadeSettings::DEFAULT);
        } else {
            setFadeSettings(FadeSettings::UPDATE_MUSIC_IN_FOCUS);
        }
        a_mtkapi_a2dp_sound_mute(TRUE);

        /* Pass AVRCP event through to Bluetooth listeners */
        EventResult event;
        event.avrcpEvent = AVRCP::PLAY_PAUSE;
        event.result = EventResult::Result::OK;
        sinkEventCallbackLocked(SinkEvent::AVRCP_IN, event);
    }
    lock.unlock();

    /* isPlaying state changed. Save it  */
    std::lock_guard<std::mutex> guard(mutex_);
    persistCurrentNetwork();
}

void MT8516BluetoothImpl::handleTrackMeta(const BT_AVRCP_MEDIA_INFO& trackMeta)
{
    YIO_LOG_INFO("Player Meta information: Title: " << trackMeta.title << ". Artist: " << trackMeta.artist
                                                    << ". Album: " << trackMeta.album << ". Genre: " << trackMeta.genre
                                                    << ". Position: " << trackMeta.position);
    EventResult eventResult;
    eventResult.result = EventResult::Result::OK;
    eventResult.avrcpEvent = AVRCP::TRACK_META_INFO;
    /* FIXME: Make sure that these strings are correct UTF8 symbols */
    if (std::strlen(trackMeta.title) > 0) {
        eventResult.trackInfo.title = trackMeta.title;
    }
    if (std::strlen(trackMeta.artist) > 0) {
        eventResult.trackInfo.artist = trackMeta.artist;
    }
    if (std::strlen(trackMeta.album) > 0) {
        eventResult.trackInfo.album = trackMeta.album;
    }
    if (std::strlen(trackMeta.genre) > 0) {
        eventResult.trackInfo.genre = trackMeta.genre;
    }
    eventResult.trackInfo.songLenMs = -1;
    eventResult.trackInfo.currPosMs = -1;
    /* Send meta information to listeners */
    sinkEventCallbackLocked(SinkEvent::AVRCP_IN, eventResult);
}

void MT8516BluetoothImpl::mtkrpcapi_BtAppGapEventCbk(tBTMW_GAP_STATE* bt_event, void* pv_tag) {
    if (!bt_event || !pv_tag) {
        return;
    }
    MT8516BluetoothImpl* this_ = reinterpret_cast<MT8516BluetoothImpl*>(pv_tag);
    this_->gapCallback(bt_event);
}

void MT8516BluetoothImpl::gapCallback(tBTMW_GAP_STATE* bt_event)
{
    YIO_LOG_INFO("Bt GAP CALLBACK: " << bt_event->state);
    switch (bt_event->state) {
        case GAP_STATE_ON: {
            YIO_LOG_INFO("GAP_STATE_ON");
            std::lock_guard<std::mutex> guard(powerStateMutex_);
            isPoweredOn_ = true;
            powerStateCondVar_.notify_one();
            break;
        }
        case GAP_STATE_OFF: {
            std::lock_guard<std::mutex> guard(powerStateMutex_);
            isPoweredOn_ = false;
            powerStateCondVar_.notify_one();
            YIO_LOG_INFO("GAP_STATE_OFF");
            break;
        }
        case GAP_STATE_ACL_CONNECTED: {
            YIO_LOG_INFO("GAP_STATE_ACL_CONNECTED: " << bt_event->bd_addr);
            BLUETOOTH_DEVICE btDeviceInfo;
            memset((void*)&btDeviceInfo, 0x00, sizeof(btDeviceInfo));
            /* Get more information about connected device */
            const int ret = a_mtkapi_bt_gap_get_dev_info(&btDeviceInfo, bt_event->bd_addr);
            std::lock_guard<std::mutex> guard(mutex_);
            if (connectedDevices_.contains(bt_event->bd_addr)) {
                break;
            }
            if (ret == BT_SUCCESS) {
                YIO_LOG_INFO("Connected device info: Name: " << btDeviceInfo.name << ". Rssi: " << btDeviceInfo.rssi << ". Cod: " << btDeviceInfo.cod);
                connectedDevices_[bt_event->bd_addr].name = btDeviceInfo.name;
            }
            /* Save connection for AVRCP Events */
            connectedDevices_[bt_event->bd_addr].role = BtRole::SOURCE;
            connectedDevices_[bt_event->bd_addr].addr = bt_event->bd_addr;
            EventResult res;
            res.result = EventResult::Result::OK;
            res.network = connectedDevices_[bt_event->bd_addr];
            sinkEventCallbackLocked(SinkEvent::CONNECTED, res);

            persistCurrentNetwork();
            break;
        }
        case GAP_STATE_ACL_DISCONNECTED: {
            YIO_LOG_INFO("GAP_STATE_ACL_DISCONNECTED. Reason: " << int(bt_event->reason) << " Addr: " << bt_event->bd_addr);
            std::lock_guard<std::mutex> guard(mutex_);
            if (!connectedDevices_.contains(bt_event->bd_addr)) {
                break;
            }
            EventResult res;
            res.result = EventResult::Result::OK;
            res.network = connectedDevices_[bt_event->bd_addr];
            connectedDevices_.erase(bt_event->bd_addr);
            if (activeSource_ == bt_event->bd_addr) {
                activeSource_.clear();
                isPlaying_ = false;
            }
            sinkEventCallbackLocked(SinkEvent::DISCONNECTED, res);

            persistCurrentNetwork();
            break;
        }
        case GAP_STATE_BONDED: {
            YIO_LOG_INFO("GAP_STATE_BONDED");
            break;
        }
        case GAP_STATE_BONDING: {
            YIO_LOG_INFO("GAP_STATE_BONDING");
            break;
        }
        case GAP_STATE_NO_BOND: {
            YIO_LOG_INFO("GAP_STATE_NO_BOND ");
            break;
        }
        case GAP_STATE_UNPAIR_SUCCESS: {
            YIO_LOG_INFO("GAP_STATE_UNPAIR_SUCCESS ");
            break;
        }
        case GAP_STATE_DISCOVERY_STARTED: {
            /* scan started  */
            YIO_LOG_INFO("GAP_STATE_DISCOVERY_STARTED ");
            break;
        }
        case GAP_STATE_DISCOVERY_STOPED: {
            /* scan ended  */
            YIO_LOG_INFO("GAP_STATE_DISCOVERY_STOPED ");
            break;
        }
        default: {
            YIO_LOG_INFO("Unknown Callback Event");
            break;
        }
    }
}

void MT8516BluetoothImpl::mtkrpcapi_BtAppGapGetPairingKeyCbk(pairing_key_value_t* /* bt_pairing_key */, UINT8* /* fg_accept */, void* /* pv_tag */)
{
    YIO_LOG_INFO("PAIRING CALLBACK KEY");
    /* TODO: Work with Pairing key or disable KEY for pairing */
}

void MT8516BluetoothImpl::mtkrpcapi_BtAppGapInquiryResponseCbk(tBTMW_GAP_DEVICE_INFO* pt_result, void* pv_tag)
{
    MT8516BluetoothImpl* this_ = reinterpret_cast<MT8516BluetoothImpl*>(pv_tag);
    std::lock_guard<std::mutex> guard(this_->mutex_);
    if (pt_result->device_kind == BT_DEVICE_SCAN) {
        Bluetooth::BtNetwork network;
        network.name = pt_result->device.name;
        network.addr = pt_result->device.bdAddr;

        YIO_LOG_INFO(pt_result->device.name << ' ' << std::hex << pt_result->device.service << ' ' << pt_result->device.devicetype << ' ' << pt_result->device.cod);
        network.role = Bluetooth::BtRole::SINK;

        this_->networks_.insert(network);
    }
    /* Fixme: Check if it's possible to understand that scan is over via callbacks */
}

bool MT8516BluetoothImpl::isAsSinkPlaying() const {
    return isPlaying_.load();
}

/* Save information about current network. Should be called under mutex_ */
void MT8516BluetoothImpl::persistCurrentNetwork() const {
    Json::Value currentNetworkJson;
    currentNetworkJson["activeSource"] = activeSource_;
    currentNetworkJson["isPlaying"] = isPlaying_.load();
    if (!connectedDevices_.empty()) {
        currentNetworkJson["connectedDevices"] = Json::arrayValue;
        for (const auto& [addr, device] : connectedDevices_) {
            currentNetworkJson["connectedDevices"].append(addr);
        }
    }
    try {
        // no need to use AtomicFile here. It is already synchronized with mutex_
        PersistentFile file(currentConnectionCacheFile_, PersistentFile::Mode::TRUNCATE);
        file.write(jsonToString(currentNetworkJson));
    } catch (const std::runtime_error& e) {
        YIO_LOG_ERROR_EVENT("MT8516BluetoothImpl.ErrorWritingConnectionsToFile", e.what());
    }
}

void MT8516BluetoothImpl::restoreCurrentNetwork()
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (fileExists(currentConnectionCacheFile_)) {
        const std::string jsonString = getFileContent(currentConnectionCacheFile_);
        auto jsonPtr = tryParseJson(jsonString);
        if (jsonPtr) {
            YIO_LOG_INFO("Apply restored bluetooth network: " << jsonString);
            /* Get saved most important information. Without bt addr it will be not possible to control bt.
             * it is okay if it is empty
             */
            activeSource_ = (*jsonPtr)["activeSource"].asString();
            /* If have bt addr -> check isPlaying. Otherwise skip it to avoid possible invalid values */
            if (!activeSource_.empty()) {
                isPlaying_ = (*jsonPtr)["isPlaying"].asBool();
            }

            if (jsonPtr->isMember("connectedDevices")) {
                for (const auto& addr : (*jsonPtr)["connectedDevices"]) {
                    connectedDevices_[addr.asString()].addr = addr.asString();
                }
            }
        }
    }
}

void MT8516BluetoothImpl::factoryReset()
{
    YIO_LOG_INFO("MT8516BluetoothImpl Factory Reset. Power off BT and then restore settingss");
    a_mtkapi_bt_gap_on_off(FALSE);
    auto resetLambda = [this]() {
        std::unique_lock<std::mutex> lock(powerStateMutex_);
        powerStateCondVar_.wait_for(lock, std::chrono::seconds(5), [this]() {
            return isPoweredOn_ == false;
        });
        YIO_LOG_INFO("Bluetooth is " << (isPoweredOn_ ? "enabled" : "not enabled yet") << ". Make Factory Reset.");
        lock.unlock();
        /* Call factory reset to restore bluetooth settings */
        a_mtkapi_bt_bluetooth_factory_reset();
        sync();
        Bluetooth::EventResult result;
        result.result = Bluetooth::EventResult::Result::OK;
        baseEventCallbackLocked(BaseEvent::FACTORY_RESET, result);
    };
    factoryResetExecutor_ = std::make_unique<PeriodicExecutor>(resetLambda, std::chrono::seconds(0), PeriodicExecutor::PeriodicType::ONE_SHOT);
}
