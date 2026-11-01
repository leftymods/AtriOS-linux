#include "alsa_audio_writer.h"

#include "alsa_error.h"

#include <yandex_io/libs/logging/logging.h>

#include <stdexcept>
#include <string>

YIO_DEFINE_LOG_MODULE("alsa");

using namespace quasar;

namespace {
    void show_available_sample_formats(snd_pcm_t* handle, snd_pcm_hw_params_t* params)
    {
        int format;

        YIO_LOG_INFO("Available formats:");
        for (format = 0; format < SND_PCM_FORMAT_LAST; format++) {
            if (snd_pcm_hw_params_test_format(handle, params, (snd_pcm_format_t)format) == 0) {
                YIO_LOG_INFO("- " << snd_pcm_format_name((snd_pcm_format_t)format));
            }
        }
    }
} // namespace

void AlsaAudioWriter::open(const std::string& deviceName, int numberOfChannels, unsigned int rate, snd_pcm_format_t format)
{
    if (device_ && hwParams_) {
        /* If alsa is already opened -> check if params changed */
        if (!checkParamsDiffer(deviceName, numberOfChannels, rate, format)) {
            /* Params didn't change. Alsa device is already opened with same params */
            return;
        }
    }
    deviceName_ = deviceName;

    hwChannels_ = numberOfChannels;
    format_ = format;
    rate_ = rate;

    tryRecover();
}

bool AlsaAudioWriter::checkParamsDiffer(const std::string& deviceName, int numberOfChannels, unsigned int rate, snd_pcm_format_t format) const {
    if (hwChannels_ != numberOfChannels) {
        return true;
    }
    if (format_ != format) {
        return true;
    }
    if (rate_ != rate) {
        return true;
    }
    if (deviceName_ != deviceName) {
        return true;
    }
    return false;
}

void AlsaAudioWriter::tryRecover()
{
    if (device_)
    {
        snd_pcm_close(device_);
        device_ = nullptr;
    }

    if ((err_ = snd_pcm_open(&device_, deviceName_.c_str(), SND_PCM_STREAM_PLAYBACK, 0)) < 0)
    {
        throw std::runtime_error(std::string("Cannot open audio device ") + deviceName_ + ": " + getError());
    }

    if (!hwParams_)
    {
        if ((err_ = snd_pcm_hw_params_malloc(&hwParams_)) < 0)
        {
            throw std::runtime_error(std::string("Cannot allocate hardware parameter structure: ") + getError());
        }
    }

    if ((err_ = snd_pcm_hw_params_any(device_, hwParams_)) < 0)
    {
        throw std::runtime_error(std::string("Cannot initialize hardware parameter structure: ") + getError());
    }

    if ((err_ = snd_pcm_hw_params_set_access(device_, hwParams_, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
    {
        throw std::runtime_error(std::string("Cannot set access type to RW_INTERLEAVED: ") + getError());
    }

    if ((err_ = snd_pcm_hw_params_set_format(device_, hwParams_, format_)) < 0)
    {
        show_available_sample_formats(device_, hwParams_);
        throw std::runtime_error(std::string("Cannot set sample format: ") + getError());
    }

    if ((err_ = snd_pcm_hw_params_set_channels(device_, hwParams_, hwChannels_)) < 0)
    {
        throw std::runtime_error(std::string("Cannot set channel count to ") + std::to_string(hwChannels_) + ": " + getError());
    }

    if ((err_ = snd_pcm_hw_params_set_rate_near(device_, hwParams_, &rate_, nullptr)) < 0)
    {
        throw std::runtime_error(std::string("Cannot set sample rate to ") + std::to_string(rate_) + ": " + getError());
    }

    /* Buffer size setting magic (from https://github.com/bear24rw/alsa-utils/blob/master/aplay/aplay.c) */

    unsigned int bufferTime{};

    if ((err_ = snd_pcm_hw_params_get_buffer_time_max(hwParams_, &bufferTime, nullptr)) < 0)
    {
        throw std::runtime_error(std::string("Cannot get max buffer size: ") + getError());
    }

    if (bufferTime > 500000) {
        bufferTime = 500000;
    }

    unsigned int periodTime = bufferTime / 4;

    if ((err_ = snd_pcm_hw_params_set_period_time_near(device_, hwParams_, &periodTime, nullptr)) < 0)
    {
        throw std::runtime_error(std::string("Cannot set period time: ") + getError());
    }

    if ((err_ = snd_pcm_hw_params_set_buffer_time_near(device_, hwParams_, &bufferTime, nullptr)) < 0)
    {
        throw std::runtime_error(std::string("Cannot set buffer time: ") + getError());
    }

    if ((err_ = snd_pcm_hw_params_get_buffer_size(hwParams_, &bufferSize_)) < 0) {
        throw std::runtime_error(std::string("Cannot get buffer size for playback: ") + getError());
    }

    if ((err_ = snd_pcm_hw_params(device_, hwParams_)) < 0)
    {
        throw std::runtime_error(std::string("Cannot set hw parameters: ") + getError());
    }

    bitsPerSample_ = snd_pcm_format_physical_width(format_);
    bitsPerFrame_ = bitsPerSample_ * hwChannels_;

    snd_pcm_hw_params_get_period_size(hwParams_, ((long unsigned*)&chunkSize_), nullptr);

    if (!swParams_)
    {
        if ((err_ = snd_pcm_sw_params_malloc(&swParams_)) < 0)
        {
            throw std::runtime_error(std::string("Cannot allocate software parameter structure: ") + getError());
        }
    }

    /* get the current swparams */
    if ((err_ = snd_pcm_sw_params_current(device_, swParams_)) < 0)
    {
        throw std::runtime_error(std::string("Cannot initialize software parameter structure: ") + getError());
    }

    /* start the transfer when the buffer is almost full */
    if ((err_ = snd_pcm_sw_params_set_start_threshold(device_, swParams_, (bufferSize_ / chunkSize_) * chunkSize_)) < 0)
    {
        throw std::runtime_error(std::string("Cannot set up a minimum buffer threshold: ") + getError());
    }

    /* allow the transfer when at least period_size samples can be processed */
    if ((err_ = snd_pcm_sw_params_set_avail_min(device_, swParams_, chunkSize_)) < 0)
    {
        throw std::runtime_error(std::string("Cannot set avail min size: ") + getError());
    }

    /* write the parameters to the playback device */
    if ((err_ = snd_pcm_sw_params(device_, swParams_)) < 0)
    {
        throw std::runtime_error(std::string("Cannot set sw parameters: ") + getError());
    }

    if ((err_ = snd_pcm_prepare(device_)) < 0)
    {
        throw std::runtime_error(std::string("Cannot prepare audio interface for use: ") + getError());
    }
}

void AlsaAudioWriter::setSampleRate(unsigned int rate)
{
    if (rate_ == rate) {
        return;
    }
    rate_ = rate;
    tryRecover();
}

bool AlsaAudioWriter::write(const std::vector<int16_t>& data)
{
    return write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(data[0]));
}

bool AlsaAudioWriter::write(const char* data, size_t numBytes)
{
    auto numSamples = numBytes / (bitsPerFrame_ / 8);
    size_t ret{};

    YIO_LOG_TRACE("Writing: frames = " << numSamples << " periodSize = " << chunkSize_ << " bitsPerSample = " << bitsPerSample_);

    while (numSamples > 0) {
        const int res = snd_pcm_writei(device_, data, numSamples);
        if (res == -EPIPE) {
            YIO_LOG_WARN(">>> Underrun <<< " << res);
            snd_pcm_prepare(device_);
        } else if (res == -EAGAIN || (res >= 0 && (size_t)res < numSamples)) {
            YIO_LOG_WARN("pcm_write err: " << res);
            snd_pcm_wait(device_, 100);
        } else if (res < 0) {
            YIO_LOG_ERROR_EVENT("AlsaAudioWriter.AlsaWriteFail", "Write failed");
            err_ = res;
            return false;
        }

        if (res > 0) {
            numSamples -= res;
            ret += res;
            data += res * bitsPerFrame_ / 8;

            YIO_LOG_TRACE("Write: " << res << " samples");
        }
    }

    YIO_LOG_TRACE("Written " << ret << " bytes");

    return true;
}

std::string AlsaAudioWriter::getError() const {
    return alsaErrorTextMessage(err_);
}

void AlsaAudioWriter::close()
{
    YIO_LOG_TRACE("Close alsa device");
    if (device_) {
        snd_pcm_drain(device_);
        snd_pcm_close(device_);
        device_ = nullptr;
    }

    if (hwParams_) {
        snd_pcm_hw_params_free(hwParams_);
        hwParams_ = nullptr;
    }
    if (swParams_) {
        snd_pcm_sw_params_free(swParams_);
        swParams_ = nullptr;
    }
}

AlsaAudioWriter::~AlsaAudioWriter()
{
    close();
}
