#include "tiny_alsa_audio_reader.h"

#include <yandex_io/libs/logging/logging.h>

#include <util/system/yassert.h>

YIO_DEFINE_LOG_MODULE("tinyalsa");

using namespace quasar;

bool TinyAlsaAudioReader::link(TinyAlsaAudioReader& device1, TinyAlsaAudioReader& device2)
{
    if (device1.device_ == nullptr || device2.device_ == nullptr) {
        return false;
    }
    if (pcm_link(device1.device_, device2.device_) != 0) {
        YIO_LOG_ERROR_EVENT("TinyAlsaAudioReader.PcmLinkFail", "Can't link pcm devices: " << device1.getError());
        return false;
    }
    return true;
}

void TinyAlsaAudioReader::open(int cardNumber, int deviceNumber, int channelCount, unsigned int rate,
                               pcm_format format, int periodSize, int periodCount)
{
    cardNumber_ = cardNumber;
    deviceNumber_ = deviceNumber;

    config_.channels = static_cast<unsigned int>(channelCount);
    config_.rate = rate;
    config_.period_size = static_cast<unsigned int>(periodSize);
    config_.period_count = static_cast<unsigned int>(periodCount);
    config_.format = format;
    config_.start_threshold = 0;
    config_.stop_threshold = 0;
    config_.silence_threshold = 0;

    tryRecover();
    YIO_LOG_INFO("Opened alsa device: " << cardNumber_ << ':' << deviceNumber_ << " Channels: " << channelCount << " Rate: " << rate
                                        << " Buffer Size: " << pcm_get_buffer_size(device_));
}

void TinyAlsaAudioReader::close() {
    if (device_) {
        YIO_LOG_INFO("Closing alsa device: " << cardNumber_ << ':' << deviceNumber_);
        pcm_close(device_);
        device_ = nullptr;
        YIO_LOG_INFO("Device closed");
    } else {
        YIO_LOG_INFO("Device's been already closed");
    }
}

void TinyAlsaAudioReader::start()
{
    if (!device_) {
        return;
    }
    const auto res = pcm_start(device_);
    if (res != 0) {
        YIO_LOG_WARN("prcm_start error: " << getError());
    }
}

void TinyAlsaAudioReader::stop()
{
    if (!device_) {
        return;
    }
    const auto res = pcm_stop(device_);
    if (res != 0) {
        YIO_LOG_WARN("prcm_stop error: " << getError());
    }
}

bool TinyAlsaAudioReader::wait(int timeoutMs)
{
    if (!device_) {
        return false;
    }
    const auto res = pcm_wait(device_, timeoutMs);
    if (res == 1) {
        return true;
    }
    if (res == 0) {
        YIO_LOG_WARN("Pcm is not ready in timeout");
    }
    if (res < 0) {
        YIO_LOG_WARN("pcm_wait error: " << getError());
    }
    return false;
}

unsigned int TinyAlsaAudioReader::getBufferSize()
{
    if (!device_) {
        return -1;
    }
    return pcm_get_buffer_size(device_);
}

bool TinyAlsaAudioReader::read(std::vector<uint8_t>& data, int frameCount)
{
    data.resize(pcm_frames_to_bytes(device_, frameCount));
    std::span<uint8_t> dataSpan(data);
    return read(std::as_writable_bytes(dataSpan), frameCount);
}

bool TinyAlsaAudioReader::read(std::span<std::byte> data, int frameCount)
{
    Y_VERIFY(frameCount >= 0);
    int res = pcm_readi(device_, data.data(), frameCount);
    if (res < 0) {
        YIO_LOG_ERROR_EVENT("TinyAlsaAudioReader.PcmReadFail", "pcm_readi error " << res << ": " << pcm_get_error(device_));
    } else if (res != frameCount) {
        YIO_LOG_ERROR_EVENT("TinyAlsaAudioReader.PcmReadNotEnoughFrames", "pcm_readi read less frames than expected. Expected: " << frameCount << " Read: " << res);
    }
    return res >= 0;
}

void TinyAlsaAudioReader::tryRecover()
{
    close();

    pcm* device = pcm_open(cardNumber_, deviceNumber_, PCM_IN, &config_);
    if (!device || !pcm_is_ready(device))
    {
        throw std::runtime_error("Unable to open pcm card: " + std::to_string(cardNumber_) + " device: " +
                                 std::to_string(deviceNumber_) + std::string(": ") + pcm_get_error(device));
    }
    device_ = device;
}

std::string TinyAlsaAudioReader::getError() const {
    if (!device_) {
        return "Device is closed";
    } else {
        return pcm_get_error(device_);
    }
}

int TinyAlsaAudioReader::getFrameRate() const {
    return config_.rate;
}

TinyAlsaAudioReader::~TinyAlsaAudioReader()
{
    close();
}
