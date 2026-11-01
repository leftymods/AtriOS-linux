#pragma once

#include "color_config.h"
#include "frame_buffer_output_frame.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/framebuffer_led_controller.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/bgr24.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/gif_recorder.h>

#include <smart_devices/libs/brightness_control/brightness_animator.h>
#include <smart_devices/libs/brightness_control/brightness_config.h>
#include <smart_devices/libs/brightness_control/brightness_control.h>

#include <linux/fb.h>

#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/modules/clock_display_state_holder/clock_display_state_holder.h>
#include <yandex_io/modules/leds/led_controller/led_device.h>

#include <fstream>
#include <string>

class FrameBufferOutputFrame; // circular

// frontal leds
class YandexStationFrontalController: public quasar::LedDevice, public BrightnessControl::Listener {
public:
    static constexpr int WIDTH = 25;
    static constexpr int HEIGHT = 16;
    static constexpr quasar::Shape screenShape{WIDTH, HEIGHT};

    YandexStationFrontalController(std::string sysfsDeviceName, std::shared_ptr<ClockDisplayStateHolder> clockDisplayStateHolder);

    void drawFrame(const FrameBufferOutputFrame& frame);

    virtual void drawBuffer(const Frame& frame);

    [[nodiscard]] int getLedCount() const override;

    YandexStationFrontalController(const YandexStationFrontalController& other) = delete;
    YandexStationFrontalController(YandexStationFrontalController&& _) = delete;

    int getWidth() const override;

    int getHeight() const override;

    void clearFrame() override;

    ColorConfig& getColorConfig();

    BrightnessConfig& getBrightnessConfig();

    std::string getLastAnimationName() const;

    void setLastAnimationName(std::string lastAnimationName);

    int getBrightness() const;

    void setBrightness(uint32_t brightness);

    void initService(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory);

protected:
    YandexStationFrontalController(); // tests only!

private:
    void onBrightnessChanged(float sensorBrightness) override;

    void handleQuasarMessage(const quasar::ipc::SharedMessage& msg);

    void captureStart();
    void captureStop();
    void captureFrame(const Frame& frame);

private:
    bool isInitialized_ = false;
    bool firstFrame_ = true;

    std::shared_ptr<ClockDisplayStateHolder> clockDisplayStateHolder_;
    FramebufferLedController internalLedController_;
    YandexStationFrontalController& operator=(const YandexStationFrontalController& other) = delete;
    ColorConfig colorConfig_;
    BrightnessConfig brightnessConfig_;
    BrightnessAnimator brightnessAnimator_;

    uint32_t currentBrightness_ = 100;

    std::string brightnessPath_;

    std::mutex recorderMutex_;
    std::unique_ptr<quasar::GifRecorder> recorder_;
    std::chrono::steady_clock::time_point lastFrameTime_;

    std::shared_ptr<quasar::ipc::IServer> server_;
    std::string lastAnimationName_;
    mutable std::mutex mutex_;
};
