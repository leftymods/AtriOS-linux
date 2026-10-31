#include "access_point.h"

#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/protos/quasar_proto.pb.h>

#include <cstdlib>
#include <ctime>
#include <tuple>

using namespace quasar;
using namespace quasar::proto;

/**
 * By default, cold start calls start()
 */
void AccessPoint::coldStart(const std::string& accessPointName) {
    start(accessPointName);
}

ScriptsAccessPoint::ScriptsAccessPoint(std::shared_ptr<YandexIO::IDevice> device, std::string startScriptPath, std::string stopScriptPath)
    : device_(std::move(device))
    , startScriptPath_(std::move(startScriptPath))
    , stopScriptPath_(std::move(stopScriptPath))
{
    // No operations.
}

void ScriptsAccessPoint::coldStart(const std::string& accessPointName) {
    YIO_LOG_INFO("Executing startap.sh");
    std::ignore = ::system((startScriptPath_ + " \"" + accessPointName + "\"").c_str());

    started_ = true;
    YIO_LOG_INFO("finished.");
}

void ScriptsAccessPoint::start(const std::string& accessPointName) {
    YIO_LOG_INFO("Executing startap.sh");
    std::ignore = ::system((startScriptPath_ + " \"" + accessPointName + "\"").c_str());
    started_ = true;
    YIO_LOG_INFO("finished.");
}

void ScriptsAccessPoint::stop() {
    YIO_LOG_INFO("Executing stopap.sh");
    std::ignore = ::system(stopScriptPath_.c_str());
    started_ = false;
    YIO_LOG_INFO("finished.");
}

bool ScriptsAccessPoint::isStarted() const {
    return started_;
}
