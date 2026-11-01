#include "device.h"
#include "yandex_station2_hal.h"

#include <smart_devices/libs/device_id/android_property/device_id.h>
#include <yandex_io/libs/android_support/system_property/system_property.h>

#include <string>

using namespace quasar;

uint8_t getColorLetter(const YandexIO::DeviceID& deviceId) {
    if (deviceId.size() == 14 && deviceId[0] == 'X') {
        return deviceId[13];
    } else if (deviceId.size() == 32 && deviceId[0] == 'X') {
        return deviceId[1];
    }
    return '?';
}

std::string getDeviceColor(uint8_t deviceColorChar) {
    switch (deviceColorChar) {
        case 'K':
            return "black";
        case 'W':
            return "white";
        case 'B':
            return "blue";
        case 'R':
            return "red";
        default:
            return std::string();
    }
}

std::string getRevision() {
    std::ostringstream result;
    result << "rev" << android_support::getSystemProperty("ro.boot.hardware.revision");
    return result.str();
}

QuasarCallParams getQuasarCallParams(int argc, char** argv) {
    auto deviceId = makeDeviceIdFromProperty("ro.serialno");
    return QuasarCallParams({
        .argc = argc,
        .argv = argv,
        .deviceId = deviceId,
        .hal = std::make_unique<YandexStation2HAL>(),
        .color = getDeviceColor(getColorLetter(deviceId)),
        .revision = getRevision(),
    });
}
