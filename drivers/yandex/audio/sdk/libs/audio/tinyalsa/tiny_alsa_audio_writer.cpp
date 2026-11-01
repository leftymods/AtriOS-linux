#include "tiny_alsa_audio_writer.h"

#include <stdexcept>
#include <string>

using namespace quasar;

void TinyAlsaAudioWriter::open(int cardNumber, int deviceNumber, int numberOfChannels, unsigned int rate, pcm_format format, int periodSize, int periodCount)
{
    cardNumber_ = cardNumber;
    deviceNumber_ = deviceNumber;

    config_.channels = numberOfChannels;
    config_.rate = rate;
    config_.period_size = periodSize;
    config_.period_count = periodCount;
    config_.format = format;
    config_.start_threshold = 0;
    config_.stop_threshold = 0;
    config_.silence_threshold = 0;

    tryRecover();
}

void TinyAlsaAudioWriter::tryRecover()
{
    if (device_)
    {
        pcm_close(device_);
        device_ = nullptr;
    }

    pcm* device = pcm_open(cardNumber_, deviceNumber_, PCM_OUT, &config_);
    if (!device || !pcm_is_ready(device))
    {
        throw std::runtime_error("Unable to open pcm card: " + std::to_string(cardNumber_) + " device: " +
                                 std::to_string(deviceNumber_) + std::string(": ") + pcm_get_error(device));
    }
    device_ = device;
}

bool TinyAlsaAudioWriter::write(const std::vector<int16_t>& data)
{
    return (pcm_write(device_, data.data(), data.size() * sizeof(data[0])) == 0);
}

std::string TinyAlsaAudioWriter::getError() const {
    if (!device_) {
        return "Device is closed";
    } else {
        return pcm_get_error(device_);
    }
}

TinyAlsaAudioWriter::~TinyAlsaAudioWriter()
{
    if (device_) {
        pcm_close(device_);
    }
}
