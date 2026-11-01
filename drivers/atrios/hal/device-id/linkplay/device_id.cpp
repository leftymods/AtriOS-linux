#include "device_id.h"

#include <smart_devices/third_party/linkplay/wmdb.h>

#define DEVICE_ID_MAX_LENGTH 64

namespace LinkplayA98 {
    std::string makeDeviceId() {
        char deviceId[DEVICE_ID_MAX_LENGTH + 1];
        wm_db_get((char*)"mv_uuid", deviceId);

        return deviceId;
    }
} // namespace LinkplayA98
