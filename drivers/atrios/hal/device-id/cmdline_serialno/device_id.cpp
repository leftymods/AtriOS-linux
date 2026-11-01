#include "device_id.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <string_view>
#include <stdexcept>

YIO_DEFINE_LOG_MODULE("device_id");

std::string quasar::makeDeviceIdFromProcCmdlineSerialno() {
    const std::string_view snNeedle = "serialno=";
    const auto cmdLine = getFileContent("/proc/cmdline");
    const auto needleBeginPos = cmdLine.find(snNeedle);
    const auto snEndPos = cmdLine.find(' ', needleBeginPos);
    if (needleBeginPos == std::string::npos || snEndPos == std::string::npos) {
        YIO_LOG_ERROR_EVENT("MakeDeviceId.Failed", "Can not obtain deviceId")
        throw std::runtime_error("Can not obtain deviceId");
    }

    const auto snBeginPos = needleBeginPos + snNeedle.size();
    return cmdLine.substr(snBeginPos, snEndPos - snBeginPos);
}
