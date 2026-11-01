#pragma once

#include <smart_devices/libs/shared_memory/shared_memory.h>

#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/bgr24.h>

#include <linux/fb.h>

#include <yandex_io/modules/leds/led_controller/led_controller.h>
#include <yandex_io/modules/leds/led_controller/led_device.h>

#include <optional>
#include <string>

#include <fcntl.h>

class FramebufferLedController: public quasar::LedDevice {
public:
    explicit FramebufferLedController(std::string sysfsDeviceName, int height, int width);

    int getWidth() const override;

    int getHeight() const override;

    void clearFrame() override;

    virtual ~FramebufferLedController();

    int getLedCount() const override;

    /**
     * Returns true if initialized successfuly.
     * Returns false if initialization fails due to known reasons (i.e. if no such device
     * registered in the system).
     * If initialization fails due to unexpected reason, will throw exception.
     */
    bool initFramebuffer();
    void panFrame() const;

    const SharedMemory<BGR24>& getFb();

    /**
     * sysfsName - name of device in sysfs (written in /sys/class/graphics/fb../name)
     * returns path to framebuffer device in /dev/ directory
     */
    static std::optional<std::string> findFramebufferByName(const std::string& sysfsName);

protected:
    fb_var_screeninfo fbVarScreeninfo_{};
    SharedMemory<BGR24> fb_{};
    int height_;
    int width_;

private:
    int deviceFd_ = -1;
    std::string sysfsDeviceName_;
};
