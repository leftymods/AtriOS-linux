#include "common.h"

#include <yandex_io/libs/device/device.h>

#include <json/json.h>

#include <ctime>
#include <sstream>

std::string AudioSender::formatLocalTime(const std::string& format) {
    char buffer[1024];
    std::time_t now = std::time(nullptr);

    if (std::strftime(buffer, sizeof(buffer), format.c_str(), std::localtime(&now))) {
        return std::string(buffer);
    }

    return "strftime() error";
}

std::string AudioSender::formatUnixTimestamp() {
    const time_t now = time(nullptr);

    struct tm firstJanuary1970 = {};
    firstJanuary1970.tm_hour = 0;
    firstJanuary1970.tm_min = 0;
    firstJanuary1970.tm_sec = 0;
    firstJanuary1970.tm_year = 70;
    firstJanuary1970.tm_mon = 0;
    firstJanuary1970.tm_mday = 1;
    firstJanuary1970.tm_isdst = 0;

    const time_t gmtNow = timegm(gmtime(&now));
    const time_t gmtFirstJanuary1970 = timegm(&firstJanuary1970);

    const long int nowCalculated = static_cast<long int>(difftime(gmtNow, gmtFirstJanuary1970));
    std::stringstream strstr;
    strstr << nowCalculated;

    return strstr.str();
}
