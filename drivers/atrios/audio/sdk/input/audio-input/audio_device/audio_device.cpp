#include "audio_device.h"

using namespace YandexIO;

AudioDevice::~AudioDevice() = default;

void AudioDevice::startCommunicationMode() {
    throw std::runtime_error("Not implemented");
}

void AudioDevice::stopCommunicationMode() {
    throw std::runtime_error("Not implemented");
}
