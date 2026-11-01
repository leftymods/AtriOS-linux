#include "alsa_audio_reader.h"

#include "alsa_error.h"

#include <yandex_io/libs/logging/logging.h>

#include <sstream>

#include <unistd.h>

YIO_DEFINE_LOG_MODULE("alsa");

using namespace quasar;

namespace {
    constexpr int FIVE_MILLISEC = 5;
    // 25 iters * 5 ms = Maximum 2000 frames on 16khz
    // We don't usually read more than 1024 frames on 16khz
    constexpr int MAX_READI_ITERATIONS = 25;
} // namespace

std::string AlsaAudioReader::buildHwDeviceName(int cardNumber, int deviceNumber)
{
    std::ostringstream out;
    out << "hw:" << cardNumber << ',' << deviceNumber;
    return out.str();
}

AlsaAudioReader::AlsaAudioReader()
{
    snd_pcm_status_malloc(&status_);
}

void AlsaAudioReader::open(const std::string& deviceName, int channelCount, unsigned int rate, snd_pcm_format_t format, int periodSize, int periodCount)
{
    (void)periodCount;

    deviceName_ = deviceName;
    hwChannels_ = channelCount;
    periodSize_ = periodSize;
    format_ = format;
    rate_ = rate;
    HWsampleSize_ = snd_pcm_format_width(format) / 8;

    tryRecover();
}

void AlsaAudioReader::logStatus()
{
    int res;
    if ((res = snd_pcm_status(device_, status_)) < 0) {
        YIO_LOG_ERROR_EVENT("AlsaAudioReader.GetStatusError", "Can't get status: " + std::to_string(res) + " " + alsaErrorTextMessage(res));
        return;
    }
    YIO_LOG_ERROR_EVENT("AlsaAudioReader.ReadWriteError", "read/write error, state " + std::string(snd_pcm_state_name(snd_pcm_status_get_state(status_))));
}

bool AlsaAudioReader::read(std::vector<uint8_t>& data, int framesCount)
{
    int bytesForRead = frameSize() * framesCount;
    data.resize(bytesForRead);
    std::span<uint8_t> dataSpan(data);
    return read(std::as_writable_bytes(dataSpan), framesCount);
}

bool AlsaAudioReader::read(std::span<std::byte> data, int framesCount)
{
    std::byte* bufferBegin = data.data();
    for (int i = 0; framesCount > 0 && i < MAX_READI_ITERATIONS; ++i) {
        int r = snd_pcm_readi(device_, bufferBegin, framesCount);
        if (r == -EAGAIN || (r >= 0 && r < framesCount)) {
            std::stringstream ss;
            if (r == -EAGAIN) {
                ss << "EAGAIN";
            } else {
                ss << "Read less than wanted: read:" << r << " wanted: " << framesCount;
            }
            ss << ". Sleeping for: " << FIVE_MILLISEC << "ms";
            YIO_LOG_ERROR_EVENT("AlsaAudioReader.AlsaReadFail", ss.str());
            snd_pcm_wait(device_, FIVE_MILLISEC);
        } else if (r < 0) {
            logStatus();
            err_ = r;
            return false;
        }
        if (r > 0) {
            framesCount -= r;
            bufferBegin += r * frameSize();
        }
    }
    if (framesCount > 0) {
        YIO_LOG_ERROR_EVENT("AlsaAudioReader.ReadDataError", "Couldn't read in " + std::to_string(MAX_READI_ITERATIONS) + " iterations");
    }

    return (framesCount == 0);
}

void AlsaAudioReader::tryRecover()
{
    if (device_)
    {
        snd_pcm_close(device_);
        device_ = nullptr;
    }

    if ((err_ = snd_pcm_open(&device_, deviceName_.c_str(), SND_PCM_STREAM_CAPTURE, 0)) < 0)
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
        throw std::runtime_error(std::string("Cannot set sample format to ") + std::to_string(format_) + ": " + getError());
    }

    if ((err_ = snd_pcm_hw_params_set_period_size_near(device_, hwParams_, &periodSize_, nullptr)) < 0)
    {
        throw std::runtime_error(std::string("Cannot set period size to ") + std::to_string(periodSize_) + ": " + getError());
    }

    if ((err_ = snd_pcm_hw_params_set_rate_near(device_, hwParams_, &rate_, nullptr)) < 0)
    {
        throw std::runtime_error(std::string("Cannot set sample rate to ") + std::to_string(rate_) + ": " + getError());
    }

    if ((err_ = snd_pcm_hw_params_set_channels(device_, hwParams_, hwChannels_)) < 0)
    {
        throw std::runtime_error(std::string("Cannot set channel count to ") + std::to_string(hwChannels_) + ": " + getError());
    }

    if ((err_ = snd_pcm_hw_params(device_, hwParams_)) < 0)
    {
        throw std::runtime_error(std::string("Cannot set parameters: ") + getError());
    }

    if ((err_ = snd_pcm_prepare(device_)) < 0) {
        throw std::runtime_error(std::string("Cannot prepare audio interface for use: ") + getError());
    }
}

std::string AlsaAudioReader::getError() const {
    return alsaErrorTextMessage(err_);
}

int AlsaAudioReader::getFrameRate() const {
    return rate_;
}

AlsaAudioReader::~AlsaAudioReader()
{
    if (device_) {
        snd_pcm_close(device_);
    }
    if (hwParams_) {
        snd_pcm_hw_params_free(hwParams_);
    }
    if (status_) {
        snd_pcm_status_free(status_);
    }
}
