#include "device_id.h"

#include <yandex_io/libs/android_support/system_property/system_property.h>

namespace quasar {

    std::string makeDeviceIdFromProperty(const std::string& property) {
        return android_support::getSystemProperty(property);
    }

} /* namespace quasar */
