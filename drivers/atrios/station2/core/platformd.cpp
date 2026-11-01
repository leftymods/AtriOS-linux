#include "buttons_handler.h"
#include "metrics_collector_impl.h"
#include "rcu_buttons_controller.h"
#include "rcu_config_observer.h"
#include "yandex_station2_volume_manager.h"

#include <smart_devices/libs/amplifiers_tuner/amplifiers_tuner.h>
#include <smart_devices/libs/android_equalizer_dispatcher/android_equalizer_dispatcher.h>
#include <smart_devices/libs/brightness_control/brightness_control.h>
#include <smart_devices/libs/brightness_control/light_sensor.h>
#include <smart_devices/libs/iot/capability/iot_capability_switcher.h>
#include <smart_devices/libs/pcba/pcba_ids.h>
#include <smart_devices/platforms/yandexstation_2/platformd/audio_input/audio_input_controller.h>
#include <smart_devices/platforms/yandexstation_2/device/device.h>
#include <smart_devices/platforms/yandexstation_2/device/yandex_station2_hal.h>
#include <smart_devices/platforms/yandexstation_2/platformd/drm_keys_reworker/encryption_key_reworker.h>
#include <smart_devices/platforms/yandexstation_2/platformd/firmware_switcher/firmware_switcher.h>
#include <smart_devices/platforms/yandexstation_2/platformd/foreground/app/snake_foreground_app.h>
#include <smart_devices/platforms/yandexstation_2/platformd/foreground/foreground_activity.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/alice_dialog_error_renderer.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/demo_mode_front_led_handler.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/device_state_handler.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/draw_led_screen_directive_observer.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/led_devices_impl.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/led_directive_observer.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/led_screen_stats_reporter.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/media_state_led_player.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/mics_state_led_player.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/config_utils.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/yandex_station2_bluetooth_observer.h>

#include <yandex_io/capabilities/volume/volume_capability.h>
#include <yandex_io/interfaces/auth/connector/auth_provider.h>
#include <yandex_io/interfaces/stereo_pair/connector/stereo_pair_provider.h>
#include <yandex_io/interfaces/user_config/connector/user_config_provider.h>
#include <yandex_io/libs/configuration/configuration.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/mixed/mixed_ipc_factory.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging_handler/logging_handler.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/minidump/quasar_minidump.h>
#include <yandex_io/libs/terminate_waiter/terminate_waiter.h>
#include <yandex_io/modules/audio_input/audio_device/websocket/websocket_audio_config_handler.h>
#include <yandex_io/modules/audio_input/audio_dumper/audio_dumper.h>
#include <yandex_io/modules/audio_input/broken_mic/broken_mic_config_handler.h>
#include <yandex_io/modules/audio_input/broken_mic/broken_mic_first_configure_handler.h>
#include <yandex_io/modules/audio_input/broken_mic/broken_mic_logger.h>
#include <yandex_io/modules/audio_input/controller/dialog_mode/audio_input_dialog_mode.h>
#include <yandex_io/modules/audio_input/controller/mics_state_proxy/audio_input_controller_mics_state_proxy.h>
#include <yandex_io/modules/audio_input/controller/multiple_vqe_controller/runtime_config_handler/active_vqe_config_handler.h>
#include <yandex_io/modules/audio_input/doa_listener/doa_listener.h>
#include <yandex_io/modules/audio_input/mics_state/metrica_listener/metrica_mics_state_listener.h>
#include <yandex_io/modules/audio_input/mics_state/sdk_notifier/mics_state_sdk_notifier.h>
#include <yandex_io/modules/audio_input/vqe_speaker_volume_proxy/vqe_controller_speaker_volume_proxy.h>
#include <yandex_io/modules/bluetooth/bluetooth_module.h>
#include <yandex_io/modules/bluetooth/quasar_android/bluetooth_android.h>
#include <yandex_io/modules/configuration_mode_sound/configuration_mode_handler.h>
#include <yandex_io/modules/demo_mode/config_manager/demo_config_manager.h>
#include <yandex_io/modules/demo_mode/demo_mode_handler.h>
#include <yandex_io/modules/demo_mode/story_module/story_module.h>
#include <yandex_io/modules/equalizer_controller/dispatcher/gstreamer/gstreamer_equalizer_dispatcher.h>
#include <yandex_io/modules/equalizer_controller/equalizer_controller.h>
#include <yandex_io/modules/geolocation/geo_ipc_server/geo_ipc_server.h>
#include <yandex_io/modules/geolocation/util/util.h>
#include <yandex_io/modules/leds/led_manager/led_manager.h>
#include <yandex_io/modules/leds/led_manager/ng/led_devices.h>
#include <yandex_io/modules/metrics/auxiliary/dev_input/dev_input_aux_detector.h>
#include <yandex_io/modules/metrics/device_power_on/device_power_on_metrica.h>
#include <yandex_io/modules/personalization/bio_capability_switcher.h>
#include <yandex_io/modules/quasar_device_state/quasar_device_state_holder.h>
#include <yandex_io/modules/smart_volume/smart_volume_configurer.h>
#include <yandex_io/modules/spotter_configurer/spotter_configurer.h>
#include <yandex_io/modules/starting_greeting/starting_greeting_data.h>
#include <yandex_io/modules/starting_greeting/success_greeting_observer.h>
#include <yandex_io/modules/starting_greeting/update_greeting_observer.h>
#include <yandex_io/modules/testpoint/testpoint_peer.h>
#include <yandex_io/modules/testpoint/testpoint_service.h>
#include <yandex_io/modules/volume_manager/controllers/max_volume/max_volume_controller.h>
#include <yandex_io/modules/xiva_alice_request/xiva_push_alice_requester.h>
#include <yandex_io/scaffolding/base/utils.h>
#include <yandex_io/sdk/audio_source/i_audio_source.h>
#include <yandex_io/sdk/device_mode_observer.h>
#include <yandex_io/sdk/yandex_iosdk.h>
#include <yandex_io/tests/testlib/null_sdk/null_sdk_interface.h>

using namespace quasar;
using namespace YandexIO;

namespace {

    constexpr const char* PCBA_IDS_PATH = "/sys/devices/platform/pcba_ids/pcba_ids_drv/pcba_ids";

    std::unique_ptr<YandexIO::EqualizerDispatcherFactory> createEqualizerDispatcherFactory(std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory,
                                                                                           std::weak_ptr<YandexIO::SDKInterface> sdk) {
        auto factory = std::make_unique<YandexIO::EqualizerDispatcherFactory>();
        factory->addDispatcherType("android", [ipcFactory{std::move(ipcFactory)}]() {
            return std::make_unique<AndroidEqualizerDispatcher>(ipcFactory);
        });
        factory->addDispatcherType("gstreamer", [sdk{std::move(sdk)}]() {
            return std::make_unique<YandexIO::GStreamerEqualizerDispatcher>(sdk);
        });
        return factory;
    }

    class WifiFirmwareSwitcher: public YandexIO::BackendConfigObserver {
        static constexpr const int DEFAULT_22_INT = 22;
        static constexpr const int BACKPORT_29_INT = 29;
        static constexpr const char* CONFIG_NAME = "wifi_firmware";
        std::shared_ptr<YandexIO::ITelemetry> telemetry_;
        std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory_;
        std::shared_ptr<quasar::IWifiSwitcher> wifiSwitcher_;
        const int onBootVersion_;
        int currentVersion_;

        static int getCurrentVersion() {
            using FwVer = wifi_firmware::FwVer;
            switch (wifi_firmware::getCurrentFirmware()) {
                case FwVer::DEFAULT_22:
                    return DEFAULT_22_INT;
                case FwVer::BACKPORT_29:
                    return BACKPORT_29_INT;
                default:
                    break;
            }
            return 0;
        }

        std::function<void(bool)> makeWifiSwitcher(bool reloadDriver) {
            if (!reloadDriver) {
                return nullptr;
            }
            if (!wifiSwitcher_) {
                wifiSwitcher_ = quasar::makeMinisWifiSwitcher(ipcFactory_);
            }
            return [this](bool wifiState) {
                YIO_LOG_DEBUG("Switching wifi to " << (wifiState ? "enable" : "disable"));
                if (wifiState) {
                    wifiSwitcher_->turnWifiOn();
                } else {
                    wifiSwitcher_->turnWifiOff();
                }
            };
        }

        bool changeFirmware(int version, bool reloadDriver) {
            if (version == DEFAULT_22_INT) {
                return wifi_firmware::changeToDefault(makeWifiSwitcher(reloadDriver));
            }
            if (version == BACKPORT_29_INT) {
                return wifi_firmware::changeToBackport(makeWifiSwitcher(reloadDriver));
            }
            return false;
        }

        static std::tuple<int, bool, bool> parseConfig(const std::string& jsonConfigValue) {
            auto cfg = quasar::tryParseJson(jsonConfigValue);
            if (cfg) {
                return {
                    tryGetInt(*cfg, "version", DEFAULT_22_INT),
                    tryGetBool(*cfg, "reloadDriver", false),
                    tryGetBool(*cfg, "forceReboot", false),
                };
            }
            return {DEFAULT_22_INT, false, false};
        }

        void onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) override {
            if (configName != CONFIG_NAME) {
                return;
            }
            const auto [version, reloadDriver, forceReboot] = parseConfig(jsonConfigValue);
            if (changeFirmware(version, reloadDriver)) {
                YIO_LOG_INFO("Wifi firwmare switched " << currentVersion_ << " -> " << version);
                reportWifiFirmwareTelemetry();
                if (wifiSwitcher_) {
                    wifiSwitcher_.reset();
                }
                currentVersion_ = version;
            }

            if (currentVersion_ != onBootVersion_ && forceReboot) {
                YIO_LOG_DEBUG("Wifi firwmare " << currentVersion_ << " is different from booted " << onBootVersion_ << ". Doing reboot");
                system("reboot");
            }
        }

        static std::string makeEventJson() {
            Json::Value json = Json::objectValue;
            json["fw"] = wifi_firmware::getLoadedFirmware();
            json["driver"] = wifi_firmware::getLoadedDriver();
            return quasar::jsonToString(json);
        }

        void reportWifiFirmwareTelemetry() {
            telemetry_->reportEvent("wifiFirmware", makeEventJson());
        }

    public:
        WifiFirmwareSwitcher(const std::shared_ptr<YandexIO::ITelemetry>& telemetry,
                             const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory)
            : telemetry_(telemetry)
            , ipcFactory_(ipcFactory)
            , onBootVersion_(getCurrentVersion())
            , currentVersion_(onBootVersion_)
        {
            reportWifiFirmwareTelemetry();
        }

        // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
        void subscribeToSystemConfig(YandexIO::SDKInterface& sdk) {
            sdk.subscribeToSystemConfig(CONFIG_NAME);
        }
    };

} // namespace

int platformd(QuasarCallParams& params)
{
    TerminateWaiter waiter;

    const auto device = params.device();
    const auto ipcFactory = params.mixedIpcFactory();
    ipcFactory->setProcessName("maind");

    const auto& maindConfig = device->configuration()->getServiceConfig("maind");
    LoggingHandler loggingHandler;
    {
        loggingHandler.init(ipcFactory, "maind", device->configuration()->getFullConfig(), device->telemetry());

        /* Init minidump Handler */
        QuasarMinidump::getInstance().init("maind", maindConfig, device->telemetry(), device->softwareVersion());
    }

    sendDaemonStartMetric("maind", device);

    std::unique_ptr<PCBAIdsMetricSender> pcbaIdsMetricsSender;
    if (auto pcbaIds = loadPCBAIds(PCBA_IDS_PATH)) {
        // send metrics with pcba info from time to time
        pcbaIdsMetricsSender = std::make_unique<PCBAIdsMetricSender>(pcbaIds->toJson(), device->telemetry());
    } else {
        YIO_LOG_WARN("Can't load pcba info");
    }

    const auto sdk = std::make_shared<YandexIO::YandexIOSDK>();
    sdk->init(ipcFactory, "maind", device->deviceId());

    const auto powerOnMetricaSender = std::make_shared<YandexIO::PowerOnMetricaSender>(device->telemetry(),
                                                                                       tryGetString(maindConfig, "powerOnMetricsSentOnceGuard"),
                                                                                       tryGetString(maindConfig, "lastMonotonicClockFile"));
    sdk->addSDKStateObserver(powerOnMetricaSender);

    auto authProvider = std::make_shared<AuthProvider>(ipcFactory);

    TFsPath clockDisplayStatePath(getString(maindConfig, "clockDisplayStatePath"));
    auto clockDisplayStateHolder = std::make_shared<ClockDisplayStateHolder>(clockDisplayStatePath, sdk);

    auto ledDevices = std::make_shared<LedDevicesImpl>(device->deviceId(),
                                                       quasar::getString(maindConfig, "aliceGifLedAnimationsPath"),
                                                       clockDisplayStateHolder);
    ledDevices->getLedScreen()->initService(ipcFactory);

    auto ledManager = std::make_shared<quasar::LedManager>(ledDevices, "/system/vendor/quasar/ledpatterns");

    /* Audio Input Configuration start */
    YandexIO::AudioDumper::initSignalHandlers();
    const auto audiodConfig = device->configuration()->getServiceConfig("audiod");

    auto ioAudioSource = YandexIO::createAudioSource(ipcFactory);
    auto allChannelsClient = ioAudioSource->createClient();

    /* Configure BrokenMicDetector */
    auto brokenMicDetector = std::make_shared<YandexIO::BrokenMicLogger>(ipcFactory, audiodConfig);
    allChannelsClient->addListener(brokenMicDetector);

    auto brokenMicFirstConfigure = std::make_shared<YandexIO::BrokenMicFirstConfigureHandler>(brokenMicDetector);
    sdk->addDeviceModeObserver(brokenMicFirstConfigure);

    auto brokenMicConfigHandler = std::make_shared<YandexIO::BrokenMicConfigHandler>(brokenMicDetector);
    sdk->addBackendConfigObserver(brokenMicConfigHandler);

    auto audioDumper = std::make_shared<YandexIO::AudioDumper>(getString(audiodConfig, "dumpPath"));
    allChannelsClient->addListener(audioDumper);

    auto websocketAudioConfigHandler = std::make_shared<YandexIO::WebsocketAudioConfigHandler>("/tmp", tryGetString(audiodConfig, "device"));
    sdk->addBackendConfigObserver(websocketAudioConfigHandler);

    auto equalizerController = std::make_shared<YandexIO::EqualizerController>(std::make_unique<quasar::NamedCallbackQueue>("EqualizerController"),
                                                                               audiodConfig, sdk, device->telemetry(), createEqualizerDispatcherFactory(ipcFactory, sdk));
    sdk->addBackendConfigObserver(equalizerController);
    sdk->getEndpointStorage()->getLocalEndpoint()->addCapability(equalizerController);
    allChannelsClient->addListener(equalizerController);
    sdk->addSDKStateObserver(equalizerController);
    sdk->addDirectiveObserver(equalizerController);

    auto audioInputController = std::make_shared<AudioInputController>(device, ipcFactory, ioAudioSource,
                                                                       websocketAudioConfigHandler);
    sdk->addBackendConfigObserver(audioInputController);
    /* Proxy that will call mute/unmute when MicsStateProvider update state */
    auto micsStateProxy = std::make_shared<YandexIO::AudioInputControllerMicsStateProxy>(audioInputController);
    auto audioInputDialogModeHandler = std::make_shared<YandexIO::AudioInputDialogMode>(audioInputController);
    sdk->getAliceCapability()->addListener(audioInputDialogModeHandler);

    auto activeVqeConfigHandler = std::make_shared<YandexIO::ActiveVqeConfigHandler>(audioInputController);
    sdk->addBackendConfigObserver(activeVqeConfigHandler);

    /* Audio Input Configuration end */

    const auto spotterConfigurer = YandexIO::SpotterConfigurer::install(
        std::make_unique<quasar::NamedCallbackQueue>("SpotterConfigurer"), *sdk, device);

    // Module that provides location and timezone to services via SDK
    const auto [geoModule, geoSDKSetter] = GeolocationUtil::installModule(sdk, device, "/data/quasar/data/");
    const auto geoIpcServer = std::make_shared<GeoIpcServer>(ipcFactory); // locationd IpcServer for TimezoneService in java code
    geoModule->addListener(geoIpcServer);

    /* LED stuff */

    sdk->addBackendConfigObserver(ledDevices);
    sdk->addSDKStateObserver(ledDevices);

    BrightnessControl brightnessControl(std::make_unique<LightSensor>("iio:device0"), ledDevices->getLedScreen());
    brightnessControl.start();

    // DrawLedScreenDirectiveObserver draws custom gifs on received DRAW_LED_SCREEN directive
    auto drawLedScreenDirectiveObserver = std::make_shared<DrawLedScreenDirectiveObserver>(ledManager, ledDevices, device);

    // LedDirectiveObserver draws local gifs on variety of received directives
    auto ledDirectiveObserver = std::make_shared<LedDirectiveObserver>(ledManager, ledDevices, drawLedScreenDirectiveObserver, clockDisplayStateHolder);
    sdk->addDirectiveObserver(ledDirectiveObserver);
    sdk->addBackendConfigObserver(ledDirectiveObserver);

    std::shared_ptr<ForegroundActivity> foregroundActivity = std::make_shared<ForegroundActivity>(
        ledDevices->getLedScreen(),
        std::make_shared<SnakeForegroundApp>(sdk));
    sdk->addDirectiveObserver(foregroundActivity);

    /* Controller for most of background animations */
    std::shared_ptr<DeviceStateHandler> deviceStateHandler = std::make_shared<DeviceStateHandler>(maindConfig, ipcFactory, ledManager, ledDevices,
                                                                                                  drawLedScreenDirectiveObserver, ledDirectiveObserver, foregroundActivity, sdk);
    sdk->addSDKStateObserver(deviceStateHandler);

    std::shared_ptr<RcuButtonsController> rcuButtonsController = std::make_shared<RcuButtonsController>(sdk);
    sdk->addSDKStateObserver(rcuButtonsController);

    /* Controller for Sound Init LED Animation */
    sdk->addDeviceModeObserver(deviceStateHandler);

    sdk->addSpectrumObserver(deviceStateHandler);

    sdk->addBackendConfigObserver(deviceStateHandler);

    /* Subscribe to AudioInput data and update Doa Angle in LISTENING animation */
    auto doaListener = std::make_shared<YandexIO::DoaListener>([deviceStateHandler = std::weak_ptr<DeviceStateHandler>(deviceStateHandler)](double doa) {
        if (auto shandler = deviceStateHandler.lock()) {
            shandler->setDoaAngle(doa);
        }
    });
    allChannelsClient->addListener(doaListener);

    auto volumeManager = std::make_shared<YandexStation2VolumeManager>(device, ipcFactory, sdk,
                                                                       "/data/quasar/last_vendor_volume",
                                                                       "/data/quasar/is_speaker_muted",
                                                                       ledManager,
                                                                       deviceStateHandler->getVolumeConductor(),
                                                                       deviceStateHandler->getRcuVolumeConductor(),
                                                                       maindConfig);
    sdk->addBackendConfigObserver(volumeManager);
    auto maxVolumeController = std::make_shared<MaxVolumeController>(volumeManager, std::make_shared<UserConfigProvider>(ipcFactory));
    maxVolumeController->start();

    ConfigurationModeHandler::Settings confModeSettings{.enterConfigurationModeFirstTimeSound = "first_enter_configure.mp3", .firstInitDefaultVolume = 4};
    auto configurationModeHandler = std::make_shared<ConfigurationModeHandler>(sdk, volumeManager, std::move(confModeSettings));
    configurationModeHandler->subscribeToSDK();

    for (const auto& vqeController : audioInputController->getVqeControllers()) {
        volumeManager->addListener(std::make_shared<VqeControllerSpeakerVolumeProxy>(vqeController));
    }

    /* Create Alice Dialog Listener. This class will animate errors of onConversationError happened */
    auto aliceDialogErrorDrawer = std::make_shared<AliceDialogErrorRenderer>(ledManager, ledDevices);
    sdk->addSDKStateObserver(aliceDialogErrorDrawer);

    /* Handler for Media States coming as a result of remote control commands */
    auto mediaStatePlayer = std::make_shared<MediaStateLedPlayer>(ledManager, ledDevices);
    sdk->addMediaObserver(mediaStatePlayer);
    sdk->addBackendConfigObserver(mediaStatePlayer);

    /* /end LED stuff */

    /* Simple interface for some Quasar States (like alice state, alarm state, etc...) */
    std::shared_ptr<QuasarDeviceStateHolder> quasarStateHolder = std::make_shared<QuasarDeviceStateHolder>();
    sdk->addSDKStateObserver(quasarStateHolder);

    std::shared_ptr<UpdateGreetingObserver> updateGreetingObserver;
    std::shared_ptr<SuccessGreetingObserver> successGreetingObserver;

    updateGreetingObserver = std::make_shared<UpdateGreetingObserver>(sdk, YandexIO::DeviceModeObserver::ConfigurationSuccessState::HAS_UPDATE,
                                                                      StartingGreetingData::CONFIGURE_SUCCESS_UPDATE_WAV);

    successGreetingObserver = std::make_shared<SuccessGreetingObserver>(sdk, YandexIO::DeviceModeObserver::ConfigurationSuccessState::NO_UPDATE,
                                                                        StartingGreetingData::CONFIGURE_SUCCESS_WAV);

    auto storyModule = std::make_shared<StoryModule>("/data/quasar/data/demo_cache/", Json::Value());
    auto demoConfigObserver = std::make_shared<DemoConfigManager>(maindConfig["demoMode"], device, storyModule);
    auto demoModeHandler = std::make_shared<DemoModeHandler>(demoConfigObserver, sdk, "/data/quasar/data/demo_mode");
    sdk->addSoundCommandObserver(demoModeHandler);
    auto demoLedHandler = std::make_shared<DemoModeFrontLedHandler>(ledManager, ledDevices, quasar::getString(maindConfig, "aliceFrontalAnimationFontsPath"));
    demoConfigObserver->addListener(demoLedHandler);
    demoModeHandler->addListener(demoLedHandler);
    demoModeHandler->addListener(updateGreetingObserver->getDemoModeListener());
    demoModeHandler->addListener(successGreetingObserver->getDemoModeListener());
    sdk->addBackendConfigObserver(demoConfigObserver);
    demoConfigObserver->subscribeToSystemConfig(*sdk);

    successGreetingObserver->subscribeToSDK();
    successGreetingObserver->subscribeToSystemConfig();
    updateGreetingObserver->subscribeToSDK();
    updateGreetingObserver->subscribeToSystemConfig();
    equalizerController->subscribeToBackendConfig();

    // clang-format off
    const auto amplifiersTuner = AmplifiersTuner::create(device, {
        .amplifiers = {AmplifiersTuner::Settings::Amplifier{
                .name = "woofer",
                .uploadNode = "/tmp/amp_woofer_settings_file",
                .defaultNode = "/tmp/amp_woofer_default_settings"
            }, AmplifiersTuner::Settings::Amplifier{
                .name = "mihi",
                .uploadNode = "/tmp/amp_mihi_settings_file",
                .defaultNode = "/tmp/amp_mihi_default_settings"
            }},
        .stateFile = "/data/quasar/data/amplifiers/state.json",
        .cacheDir = "/data/quasar/data/amplifiers/cache/",
        .uploadDir = "/tmp/amplifier_fw"}, *sdk);
    // clang-format on

    /* Bluetooth Observers*/
    auto bluetoothConnector = std::make_unique<YandexIO::BluetoothAndroid>(ipcFactory);
    YandexIO::BluetoothModule::Settings settings;
    settings.useBackendConfigObserver = false;
    settings.telemetry = device->telemetry();
    settings.telemetrySettings = {.heartbeat = true, .connections = false, .power = false};
    auto stereoPairProvider = std::make_shared<StereoPairProvider>(device, ipcFactory);
    auto bluetoothModule = std::make_shared<YandexIO::BluetoothModule>(std::move(bluetoothConnector), stereoPairProvider, sdk, settings);
    auto bluetoothController = std::make_shared<YandexStation2BluetoothObserver>(bluetoothModule->getBluetoothController(), ledManager, ledDevices);
    bluetoothModule->addBluetoothStateListener(deviceStateHandler->getBluetoothStateListener());
    bluetoothModule->addBluetoothStateListener(volumeManager->getBluetoothStateListener());
    bluetoothModule->addBluetoothListener(bluetoothController);
    bluetoothModule->addBluetoothListener(volumeManager);

    /* Handler for fancy Mute animation  */
    auto fancyMuteAnimator = std::make_shared<MicsStateLedPlayer>(ledManager, ledDevices);
    auto buttonsHandler = std::make_shared<ButtonsHandler>(sdk, device->telemetry(),
                                                           ipcFactory, volumeManager, bluetoothController, quasarStateHolder,
                                                           foregroundActivity, rcuButtonsController, audioInputController,
                                                           drawLedScreenDirectiveObserver, demoModeHandler);
    /* Register handlers of mics state */
    buttonsHandler->micsStateProvider().addListener(micsStateProxy);
    buttonsHandler->micsStateProvider().addListener(fancyMuteAnimator);
    auto metricaMicsState = std::make_shared<YandexIO::MetricaMicsStateListener>(device->telemetry());
    buttonsHandler->micsStateProvider().addListener(metricaMicsState);
    auto micsStateSDKNotifier = std::make_shared<YandexIO::MicsStateSDKNotifier>(sdk);
    buttonsHandler->micsStateProvider().addListener(micsStateSDKNotifier);

    auto rcuConfigObserver = std::make_shared<RcuConfigObserver>(buttonsHandler);
    sdk->addBackendConfigObserver(rcuConfigObserver);

    /* init config and start */

    activeVqeConfigHandler->subscribeToConfig(*sdk);
    brokenMicConfigHandler->subscribeToConfig(*sdk);
    websocketAudioConfigHandler->subscribeToConfig(*sdk);
    ledDevices->subscribeToConfig(*sdk);

    sdk->subscribeToDeviceConfig(ConfigUtils::LED_USER_CONFIG);

    ledDirectiveObserver->subscribeToSystemConfig(*sdk);
    deviceStateHandler->subscribeToSystemConfig();
    volumeManager->subscribeToSystemConfig();
    rcuConfigObserver->subscribeToSystemConfig(*sdk);
    audioInputController->subscribeToSystemConfig(*sdk);

    // must be done after sdk init
    clockDisplayStateHolder->addListener(deviceStateHandler);

    LedScreenStatsReporter ledScreenStatsReporter(ledDevices->getLedScreen(), sdk, maindConfig);

    // convert xiva push to aliceRequests
    const auto xivaAliceRequester = YandexIO::XivaPushAliceRequester::install(*sdk);

    sdk->allowInitConfigurationState();
    volumeManager->start();
    audioInputController->start(); // start capture thread. ButtonsHandler can "mute" input when it starts
    buttonsHandler->start();       // start polling and provide initial mics state

    /* end init config and start */

    allChannelsClient->subscribeToChannels(YandexIO::RequestChannelType::ALL);
    allChannelsClient->start(); // forward audio input data to listeners

    // start boot animation
    deviceStateHandler->startBootAnimation();

    YandexIO::DevInputAuxDetector auxDetector(device->telemetry(), "/dev/input/event3", 2 /* code */);

    TestpointService testpoint(device, sdk, ipcFactory, volumeManager);
    testpoint.start();
    const auto testpointPeer = std::make_shared<YandexIO::TestpointPeer>(*ipcFactory);

    yandexstation_2::EncryptionKeyReworker reworker(device, authProvider, "/data/quasar/drmKeysProvisioned", "/vendor/bin/tee_provision", "/tmp/");

    auto wifiFwSwitcher = std::make_shared<WifiFirmwareSwitcher>(device->telemetry(), ipcFactory);
    sdk->addBackendConfigObserver(wifiFwSwitcher);
    wifiFwSwitcher->subscribeToSystemConfig(*sdk);

    auto iotCapabilitySwitcher = std::make_shared<YandexIO::IotCapabilitySwitcher>(
        sdk, device, deviceStateHandler, sdk->getEndpointStorage()->getLocalEndpoint());
    sdk->getEndpointStorage()->addListener(iotCapabilitySwitcher);

    // Personalization
    const auto bioCapabilitySwitcher = std::make_shared<YandexIO::BioCapabilitySwitcher>(sdk, device, nullptr, sdk->getEndpointStorage()->getLocalEndpoint(), authProvider, allChannelsClient);
    sdk->addBackendConfigObserver(bioCapabilitySwitcher);

    // Smart volume
    const auto smartVolumeConfigurer = YandexIO::SmartVolumeConfigurer::create(sdk, volumeManager, allChannelsClient);

    // Volume capability
    const auto volumeCapability = std::make_shared<YandexIO::VolumeCapability>(volumeManager);
    sdk->getEndpointStorage()->getLocalEndpoint()->addCapability(volumeCapability);

    waiter.wait();

    return 0;
}
