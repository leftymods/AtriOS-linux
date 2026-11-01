#pragma once

#include "private/source_stream_sender.h"
#include "private/stream_alsa_writer.h"

#include <smart_devices/third_party/linkplay_bluetooth/linkplay_bluetooth_interface.h>
#include <smart_devices/third_party/linkplay/linkplay_resample_interface.h>

#include <yandex_io/libs/base/linux_timer.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/bluetooth/bluetooth.h>
#include <yandex_io/libs/threading/periodic_executor.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>
#include <array>
#include <atomic>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>

namespace quasar {

    class LinkplayA98BluetoothImpl: public Bluetooth {
    public:
        explicit LinkplayA98BluetoothImpl(const std::string& name);

        ~LinkplayA98BluetoothImpl();

        int scanNetworks() override;

        int stopScanNetworks() override;

        /* Linkplay API does not have power on and power off functions, so this call will be always ends with errors*/
        int powerOn() override;

        int powerOff() override;

        int startStream();

        int stopStream();

        int pairWithSink(const Bluetooth::BtNetwork& network) override;

        int setVisibility(bool isDiscoverable, bool isConnectable) override;

        int disconnectAll(BtRole role) override;

        int asSinkPlayNext(const Bluetooth::BtNetwork& network) override;

        int asSinkPlayPrev(const Bluetooth::BtNetwork& network) override;

        int asSinkPlayStart(const Bluetooth::BtNetwork& network) override;

        int asSinkPlayPause(const Bluetooth::BtNetwork& network) override;

        int asSinkSetVolumeAbs(int volume) override;

        PowerState getPowerState() const override;

        bool isAsSinkPlaying() const override;

        void takeAudioFocus() override;
        void freeAudioFocus() override;
        /* Todo: implement for YandexMini */
        int setName(const std::string& /* name */) override {
            return -1;
        };

        void factoryReset() override{};

    public:
        enum BsaServerMode {
            BSA_DISABLED = 0,
            BSA_ENABLED = 1
        };
        static void setBsaServerMode(int mode);

    private:
        /* Menu items */
        enum class BtAppCommands {
            START_SINK = 0, //ready
            START_SOURCE,   //ready
            SET_VISIBILITY, //ready
            START_SCAN,     //ready
            CANCEL_SCAN,    //todo
            SET_VOLUME_ABS,
            PLAYBACK_START,  //todo
            PLAYBACK_PAUSE,  //todo
            PLAYBACK_PREV,   //todo
            PLAYBACK_NEXT,   //todo
            DISCONNECT_FROM, //todo
            DISCONNECT_ALL,  //fixme: need check that all are disconnected
            SOURCE_CONNECT,  // todo
            START_STREAM,    //ready
            STOP_STREAM      //ready
        };

        void start();

        int powerOffImpl();

        /**
     * @brief All perform functions add commands to commandsQueue.
     */
        int perform(BtAppCommands command);
        int perform(BtAppCommands command, bool discoverable, bool connectable);
        int perform(BtAppCommands command, const std::string& btAddr);
        int perform(BtAppCommands command, Bluetooth::BtRole role);
        int perform(BtAppCommands command, int volume);
        /* TODO: Add addCommand method */
        static int eventCallback(bt_event_msg* msg, void* context);

        /* Stream in callbacks handler */
        pcm_render_call_back streamInCallbacks_;

        /**
     * @brief It is commands queue consumer. dispatchCommand handle all commands in commandsQueue
     * and leave it empty
     */
        int dispatchCommand();

        void bluetoothThread();

        /**
     * @brief Callback that will be called when there is no stream during timeout
     */
        void playingTimeout();

        /**
     * @brief Will be called when playing stream. If device wasn't playing stream before -> call
     *        user callback
     */
        void handleStartPlaying();

        static std::set<Bluetooth::BtNetwork> getNetworksSetFromJson(const Json::Value& networks);

        static std::string msgTypeToString(int msgType);

        /**
     * @brief Bluetooth config. Need to keep in memory for callbacks
     */
        linkplay_bt_config btConfig_;

        /**
     * @brief the thread that parses the command queue and execute commands
     * It is using linkplay internal synchronization mechanism (like cond var)
     * So to bring up this thread need to use @see linkplay_bluetooth_schedule_event() function
     */
        std::thread btThread_;

        /**
     * @brief Realisation of Source Stream Sender for Linkplay libasound realisation
     * Linkplay libasound write each sound chanel into separate FIFO and SourceStreamSender read it,
     * mix up, form A2DP protocol and send data through linkplay BT API.
     */
        SourceStreamSender streamSender_;

        struct BtCommand {
            int cmd{0};
            std::vector<uint32_t> params;
            std::string btAddress;
            int volume;
            BtRole roleToDisconnect;
        };
        int lastCmd_{-1};
        /**
     * @brief Queue with commands, where @see perform() functions are a producer and @see dispatchCommand()
     * is a consumer function.
     */
        std::queue<BtCommand> commandsQueue_;
        std::mutex commandMutex_;

        std::string performConnectionAddr_;
        Bluetooth::BtRole performRole_;

        std::atomic_bool isPlaying_{false};
        std::string pairedSinkAddr_;

        bool sourceProfileEnabled_{false};

        int volumeToSet_ = -1;
        /**
     * @brief last scanned networks json string
     */
        std::string networksJsonStr_;

        /* String constants */
        static std::string btErrorStr(BtError btError);
        static std::string btSecEvtStr(tBT_SEC_EVT btSecEvt);
        static std::string btServEvtStr(tBT_SERV_EVT btServEvt);
        static std::string btSinkEvtStr(tBT_SINK_EVT btSinkEvt);
        static std::string btSourceEvtStr(tBT_SOURCE_EVT btSourceEvt);
        static std::string btRoleStr(int btRole);

        std::atomic_bool threadExists_;
        quasar::SteadyConditionVariable condVar_;
        std::mutex sourceConnMutex_;
        /* Current connections TO SOURCE devices (local device as SINK) */
        std::set<std::string> curSourceConnMacs_;

        std::atomic<PowerState> powerState_{PowerState::OFF};

        static int writeData(void* context, void* pcm_handle, int connection, UINT8* pbuffer, int length) noexcept;
        int handleWriteData(UINT8* pbuffer, int length);

        static void* open(void* context, UINT8 format, UINT16 sampleRate, UINT8 numChannel, UINT8 bitPerSample);
        static void close(void* context, void* pcm_handle);

        std::atomic<double> focusCoef_{1.0};
        quasar::StreamAlsaWriter streamAlsaWriter_;
        std::unique_ptr<quasar::LinuxTimer> playingTimer_;
    };

} /* namespace quasar */
