#include "light_sensor.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/errno/errno_exception.h>
#include <yandex_io/libs/logging/logging.h>

#include <util/folder/path.h>

#include <fstream>
#include <iomanip>

YIO_DEFINE_LOG_MODULE("brightness_control");

LightSensor::LightSensor(const std::string& deviceName, LightSensor::HardwareGain gain, LightSensor::IntegrationTime time)
    : devicePath_(JoinFsPaths("/sys/bus/iio/devices", deviceName))
{
    setHardwareGain(gain);
    setIntegrationTime(time);
}

void LightSensor::setHardwareGain(LightSensor::HardwareGain gain) {
    try {
        std::ofstream hardwareGain(JoinFsPaths(devicePath_, "in_intensity_hardwaregain"));
        hardwareGain << static_cast<int>(gain) << std::endl;
        YIO_LOG_INFO("Set hardware gain to " << static_cast<int>(gain));
    } catch (const std::ios_base::failure& e) {
        YIO_LOG_ERROR_EVENT("LightSensor.FailedSetHardwareGain", "Failed to set hardware gain: " << e.what());
    }
}

void LightSensor::setIntegrationTime(LightSensor::IntegrationTime time) {
    try {
        std::ofstream integrationTime(JoinFsPaths(devicePath_, "in_intensity_integration_time"));
        integrationTime << "0." << std::setw(3) << std::setfill('0') << static_cast<int>(time) << std::endl;
        YIO_LOG_INFO("Set integration time to " << static_cast<int>(time) << "ms");
    } catch (const std::ios_base::failure& e) {
        YIO_LOG_ERROR_EVENT("LightSensor.FailedSetIntegrationTime", "Failed to set integration time: " << e.what());
    }
}

float LightSensor::getBrightness() {
    const std::string luxPath = JoinFsPaths(devicePath_, "in_intensity_input");
    std::ifstream lux(luxPath);
    if (!lux.is_open()) {
        YIO_LOG_ERROR_EVENT("LightSensor.FailedOpenLuxSensor", "Error trying to open lux sensor(" << luxPath << "): " << quasar::strError(errno));
        return LightSensor::DEFAULT_BRIGHTNESS;
    }

    std::string tmp;
    try {
        lux >> tmp;
        return std::stof(tmp);
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("LightSensor.FailedReadBrightness", "Error trying to read and convert brightness (" << tmp << ") from sensor to float: " << e.what());
        return LightSensor::DEFAULT_BRIGHTNESS;
    }
}
