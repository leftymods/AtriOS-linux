#include "framebuffer_led_controller.h"

#include <linux/fb.h>

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/errno/errno_exception.h>
#include <yandex_io/libs/logging/logging.h>

#include <util/generic/scope.h>

#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

using namespace quasar;

FramebufferLedController::FramebufferLedController(std::string sysfsDeviceName, int height, int width)
    : height_(height)
    , width_(width)
    , sysfsDeviceName_(std::move(sysfsDeviceName))
{
}

bool FramebufferLedController::initFramebuffer() {
    auto deviceName = findFramebufferByName(sysfsDeviceName_);
    if (!deviceName.has_value()) {
        YIO_LOG_ERROR_EVENT("FramebufferLedController.InvalidSysfsNode", "No such led device: " << sysfsDeviceName_);
        return false;
    }
    auto deviceFileName = "/dev/graphics/" + deviceName.value();

    deviceFd_ = open(deviceFileName.c_str(), O_RDWR);
    if (deviceFd_ < 0) {
        throw quasar::ErrnoException{errno,
                                     "cannot open device '" + deviceFileName + "': "};
    }
    bool deviceFdOk = false;
    Y_DEFER {
        if (!deviceFdOk) {
            close(deviceFd_);
        }
    };

    if (ioctl(deviceFd_, FBIOGET_VSCREENINFO, &fbVarScreeninfo_) == -1) {
        throw quasar::ErrnoException{errno, std::string("cannot get framebuffer vars: ")};
    }
    fbVarScreeninfo_.xoffset = 0;
    fbVarScreeninfo_.yoffset = 0;
    if (ioctl(deviceFd_, FBIOPAN_DISPLAY, &fbVarScreeninfo_) == -1) {
        throw quasar::ErrnoException{errno, std::string("cannot move framebuffer: ")};
    }

    fb_fix_screeninfo fix{};
    if (ioctl(deviceFd_, FBIOGET_FSCREENINFO, &fix) == -1) {
        throw quasar::ErrnoException{errno, std::string("cannot get framebuffer fix info: ")};
    }

    fb_ = SharedMemory<BGR24>(nullptr, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, deviceFd_, 0);
    if (fb_.get() == nullptr) {
        throw std::runtime_error{std::string("cannot mmap framebuffer: ")};
    }

    deviceFdOk = true;

    return true;
}

int FramebufferLedController::getWidth() const {
    return width_;
}

int FramebufferLedController::getHeight() const {
    return height_;
}

void FramebufferLedController::clearFrame() {
    // TODO
}

void FramebufferLedController::panFrame() const {
    if (ioctl(deviceFd_, FBIOPAN_DISPLAY, &fbVarScreeninfo_) == -1) {
        throw quasar::ErrnoException{errno, std::string("cannot pan framebuffer: ")};
    }
}

FramebufferLedController::~FramebufferLedController() {
    if (deviceFd_ >= 0) {
        close(deviceFd_);
    }
}

int FramebufferLedController::getLedCount() const {
    return width_ * height_;
}

const SharedMemory<BGR24>& FramebufferLedController::getFb() {
    return fb_;
}

std::optional<std::string> FramebufferLedController::findFramebufferByName(const std::string& sysfsName) {
    const std::string sysClassGraphics = "/sys/class/graphics/";

    std::vector<std::string> devices;
    try {
        devices = getDirectoryFileList(sysClassGraphics);
    } catch (std::exception& e) {
        YIO_LOG_ERROR_EVENT("FramebufferLedController.ListDirFailed", "Fail to open " << sysClassGraphics << ": " << e.what());
        return std::nullopt;
    }

    for (const auto& device : devices) {
        const std::string fileName = sysClassGraphics + device + "/name";

        std::string currentDeviceName;
        try {
            currentDeviceName = trim(getFileContent(fileName));
        } catch (const std::exception& e) {
            YIO_LOG_ERROR_EVENT("FramebufferLedController.ReadFailed", "Fail to read " << fileName << ": " << e.what());
            continue;
        }

        if (sysfsName == currentDeviceName) {
            YIO_LOG_INFO("Led device '" << sysfsName << "' is mapped to " << device);
            return device;
        }
    }

    return std::nullopt;
}
