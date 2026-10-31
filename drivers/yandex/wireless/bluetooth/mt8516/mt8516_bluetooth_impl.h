#pragma once

#include <smart_devices/third_party/mediatek/mt8516/mtk_bt_service_gap_wrapper.h>
#include <smart_devices/third_party/mediatek/mt8516/rw_init_mtk_bt_service.h>
#include <smart_devices/third_party/mediatek/mt8516/u_bt_mw_a2dp.h>
#include <smart_devices/third_party/mediatek/mt8516/u_bt_mw_avrcp.h>

#include <yandex_io/libs/bluetooth/bluetooth.h>
#include <yandex_io/libs/threading/periodic_executor.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>

class MT8516BluetoothImpl final: public Bluetooth {
public:
    explicit MT8516BluetoothImpl(const std::string& name, std::string currentConnectionCacheFile);

    ~MT8516BluetoothImpl();

    int scanNetworks() override;

    int powerOn() override {
        return 0;
    }
    int powerOff() override {
        return 0;
    }
    PowerState getPowerState() const override {
        return PowerState::ON;
    }

    bool isAsSinkPlaying() const override;

    int stopScanNetworks() override {
        return 0;
    }

    int pairWithSink(const Bluetooth::BtNetwork& network) override;

    int setVisibility(bool isDiscoverable, bool isConnectable) override;

    int disconnectAll(BtRole role) override;

    int asSinkPlayNext(const Bluetooth::BtNetwork& network) override;

    int asSinkPlayPrev(const Bluetooth::BtNetwork& network) override;

    int asSinkPlayStart(const Bluetooth::BtNetwork& network) override;

    int asSinkPlayPause(const Bluetooth::BtNetwork& network) override;

    int asSinkSetVolumeAbs(int volume) override;

    void takeAudioFocus() override;

    void freeAudioFocus() override;

    int setName(const std::string& name) override;

    void factoryReset() override;

private:
    void init();
    /**
     * @brief Call mtk function to change bluetooth name
     * @brief name - name to set
     * @return - a_mtkapi_bt_gap_set_name result
     */
    static int applyNewName(const std::string& name);
    MTKRPCAPI_BT_APP_CB_FUNC mtkCallbacks_;

    /* C-style wrappers for callbacks required by mtk API. Forward to in-class callbacks (pv_tag == this) */
    static void mtkrpcapi_BtAppGapEventCbk(tBTMW_GAP_STATE* bt_event, void* pv_tag);
    static void mtkrpcapi_BtAppGapGetPairingKeyCbk(pairing_key_value_t* bt_pairing_key,
                                                   UINT8* fg_accept, void* pv_tag);
    static void mtkrpcapi_BtAppGapInquiryResponseCbk(tBTMW_GAP_DEVICE_INFO* pt_result, void* pv_tag);

    static void mtkrpcapi_BtAppA2dpCbk(BT_A2DP_EVENT_PARAM* param, void* pv_tag);
    static void mtkrpcapi_BtAppAvrcpCbk(BT_AVRCP_EVENT_PARAM* param, VOID* pv_tag);

    /* Actual Bluetooth Callbacks */
    void gapCallback(tBTMW_GAP_STATE* bt_event);
    void avrcpCallback(BT_AVRCP_EVENT_PARAM* param);
    void a2dpCallback(BT_A2DP_EVENT_PARAM* param);

    void handleTrackMeta(const BT_AVRCP_MEDIA_INFO& trackMeta);
    void handlePlayerStatusChange(bool isPlaying);
    std::mutex fadeMutex_;              /* Protect freeFocus_ and isPlaying_ variables (they can be accessed from different threads) */
    bool freeFocus_ = false;            /* Somebody captured focus. Bt stream should free focus if freeFocus_ is true */
    std::atomic_bool isPlaying_{false}; /* Is A2DP stream currently playing  */

    static constexpr int MAX_VOLUME = 0x10000;
    static constexpr int MIN_VOLUME = 0;
    static constexpr int FOCUS_VOLUME = 0x1af0; /* -40.00 dB from stream (check GainMap from mtk_bt_service_a2dp_wrapper.h */

    enum class FadeSettings {
        DEFAULT,              /* Settings for default Fade In/Out behaviour (from 0 to MAX) */
        MUSIC_FOCUS,          /* Settings to use Fade In/out for AudioFocus. Music is already playing so volume change btw (FOCUS_VOLUME and MAX)*/
        UPDATE_MUSIC_IN_FOCUS /* Settings that should be applied when Focus is already capturing, but muisc starting to play or stopping play */
    };
    /**
     * @brief Set up Fade In/Out settings using linkplay *_set_fade function
     * @param type - Setting type. Check FadeSettings description
     * @note Should be called under fadeMutex_
     */
    static void setFadeSettings(FadeSettings type);

    /**
     * @brief Save information about current bluetooth connection in file. In case if process crashes this information
     *        will be restored. Otherwise it will be not possible to control bluetooth
     */
    void persistCurrentNetwork() const;
    void restoreCurrentNetwork();
    const std::string currentConnectionCacheFile_;

    std::mutex periodicMutex_;

    /* Handle devices that does not support AVRCP Start/Stop events*/
    std::atomic_bool streamStartedWithoutAvrcp_{false};
    std::unique_ptr<quasar::PeriodicExecutor> startStreamWithoutAvrcpExecutor_;

    using BtAddr = std::string;
    std::map<BtAddr, BtNetwork> connectedDevices_;
    BtAddr activeSource_;
    std::mutex mutex_;

    std::set<Bluetooth::BtNetwork> networks_;

    std::mutex powerStateMutex_;
    bool isPoweredOn_{false};
    quasar::SteadyConditionVariable powerStateCondVar_;
    std::unique_ptr<quasar::PeriodicExecutor> factoryResetExecutor_;
};
