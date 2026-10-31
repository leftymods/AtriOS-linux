#include "yandex_station_frontal_controller.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/errno/errno_exception.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/protos/quasar_proto.pb.h>

#include <yandex_io/modules/leds/led_controller/color_readers.h>

#include <chrono>
#include <thread>
#include <utility>

using namespace quasar;

namespace {

    const BrightnessConfig::LinearCurve BRIGHTNESS_CURVE = {
        {0.0f, 3},
        {1.0f, 30},
        {10.0f, 100},
        {50.0f, 150},
        {120.0f, 200},
        {256.0f, 200}};

    constexpr uint32_t MIN_FIXED_BRIGHTNESS = 0;
    constexpr uint32_t MAX_FIXED_BRIGHTNESS = 200;

} // namespace

YandexStationFrontalController::YandexStationFrontalController(std::string sysfsDeviceName,
                                                               std::shared_ptr<ClockDisplayStateHolder> clockDisplayStateHolder)
    : clockDisplayStateHolder_(std::move(clockDisplayStateHolder))
    , internalLedController_(sysfsDeviceName, YandexStationFrontalController::HEIGHT, YandexStationFrontalController::WIDTH)
    , brightnessConfig_(BRIGHTNESS_CURVE, MIN_FIXED_BRIGHTNESS, MAX_FIXED_BRIGHTNESS)
    , brightnessAnimator_(std::bind(&YandexStationFrontalController::setBrightness, this, std::placeholders::_1))
{
    std::optional<std::string> name = FramebufferLedController::findFramebufferByName(sysfsDeviceName);
    if (name.has_value()) {
        brightnessPath_ = join(std::vector<std::string>{"/sys/class/backlight", name.value(), "brightness"}, "/");
    }
}

YandexStationFrontalController::YandexStationFrontalController()
    : internalLedController_("", 0, 0)
    , brightnessConfig_(BRIGHTNESS_CURVE, MIN_FIXED_BRIGHTNESS, MAX_FIXED_BRIGHTNESS)
    , brightnessAnimator_(std::bind(&YandexStationFrontalController::setBrightness, this, std::placeholders::_1))
{
}

void YandexStationFrontalController::drawFrame(const FrameBufferOutputFrame& frame) {
    frame.draw(this);
}

void YandexStationFrontalController::drawBuffer(const Frame& frame) {
    if (firstFrame_) {
        isInitialized_ = internalLedController_.initFramebuffer();
        firstFrame_ = false;
    }
    if (isInitialized_) {
        std::copy(std::begin(frame), std::end(frame), internalLedController_.getFb().get());
        internalLedController_.panFrame();
    }

    captureFrame(frame);
}

void YandexStationFrontalController::captureFrame(const Frame& frame) {
    std::scoped_lock guard(recorderMutex_);

    if (!recorder_) {
        return;
    }

    GifFrame gifFrame;
    gifFrame.data = frame;
    gifFrame.width = WIDTH;
    gifFrame.height = HEIGHT;

    auto now = std::chrono::steady_clock::now();

    int delayMs = 0;
    if (lastFrameTime_.time_since_epoch().count() != 0) {
        delayMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime_).count();
    }

    gifFrame.durationCentiSeconds = delayMs / 10;

    lastFrameTime_ = now;

    recorder_->record(std::move(gifFrame));
}

int YandexStationFrontalController::getLedCount() const {
    return internalLedController_.getLedCount();
}

int YandexStationFrontalController::getWidth() const {
    return internalLedController_.getWidth();
}

int YandexStationFrontalController::getHeight() const {
    return internalLedController_.getHeight();
}

void YandexStationFrontalController::clearFrame() {
    if (isInitialized_) {
        internalLedController_.clearFrame();
    }
}

ColorConfig& YandexStationFrontalController::getColorConfig() {
    return colorConfig_;
}

void YandexStationFrontalController::onBrightnessChanged(float sensorBrightness) {
    auto targetBrightness = brightnessConfig_.getLevel(sensorBrightness);

    clockDisplayStateHolder_->setBrightness((double)targetBrightness / MAX_FIXED_BRIGHTNESS);

    brightnessAnimator_.animate(targetBrightness);
}

void YandexStationFrontalController::setBrightness(uint32_t brightness) {
    if (brightnessPath_.empty()) {
        return;
    }

    try {
        std::ofstream brightnessDevice;
        brightnessDevice.exceptions(std::ios::failbit | std::ios::badbit);
        brightnessDevice.open(brightnessPath_);
        brightnessDevice << brightness;
        brightnessDevice.flush();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            currentBrightness_ = brightness;
        }
    } catch (const std::exception& e) {
        YIO_LOG_WARN("Exception trying to write device brightness to (" << brightnessPath_ << ") " << e.what());
    }
}

BrightnessConfig& YandexStationFrontalController::getBrightnessConfig() {
    return brightnessConfig_;
}

void YandexStationFrontalController::handleQuasarMessage(const ipc::SharedMessage& msg) {
    if (!msg->has_led_screen_command()) {
        return;
    }

    const auto& command = msg->led_screen_command();

    if (command.has_capture_start()) {
        captureStart();
    } else if (command.has_capture_stop()) {
        captureStop();
    }
}

void YandexStationFrontalController::captureStart() {
    std::scoped_lock guard(recorderMutex_);

    recorder_ = std::make_unique<GifRecorder>("/tmp/led.gif");

    YIO_LOG_INFO("LED screen capture started");
}

void YandexStationFrontalController::captureStop() {
    std::scoped_lock guard(recorderMutex_);

    recorder_->save();
    recorder_.reset();

    YIO_LOG_INFO("LED screen capture stopped");
}

std::string YandexStationFrontalController::getLastAnimationName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastAnimationName_;
}

void YandexStationFrontalController::setLastAnimationName(std::string lastAnimationName) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastAnimationName_ = std::move(lastAnimationName);
}

int YandexStationFrontalController::getBrightness() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentBrightness_;
}

void YandexStationFrontalController::initService(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory) {
    server_ = ipcFactory->createIpcServer("led_screen");
    server_->setMessageHandler([this](const auto& message, auto& /* connection */) {
        handleQuasarMessage(message);
    });
    server_->listenService();
}
