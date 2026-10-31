#include "source_stream_sender.h"

#include <yandex_io/libs/logging/logging.h>

#include <array>
#include <functional>
#include <sstream>
#include <string.h>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

YIO_DEFINE_LOG_MODULE("bluetooth");

SourceStreamSender::SourceStreamSender()
    : threadExists_(true)
{
    init();
}

SourceStreamSender::~SourceStreamSender()
{
    stopCaptureThread();
    if (resampleInstance_ != nullptr) {
        linkplay_resample_uninit(resampleInstance_);
    }

    close(bluetoothFifoFd_);
}

void SourceStreamSender::runCaptureThread()
{
    stopCaptureThread();
    std::lock_guard<std::mutex> guard(threadMutex_);
    threadExists_ = true;
    alsaCaptureThread_ = std::thread(std::bind(&SourceStreamSender::alsaCaptureThread, this));
}

tLINKPLAY_AV_MEDIA_FEED_CFG_PCM SourceStreamSender::getPcmConfig() const {
    std::lock_guard<std::mutex> guard(pcmConfigMutex_);
    return pcmConfig_;
}

void SourceStreamSender::stopCaptureThread()
{
    /* Prevent few stopCaptureThread calls at one moment */
    std::lock_guard<std::mutex> guard(threadMutex_);
    threadExists_ = false;
    if (alsaCaptureThread_.joinable()) {
        /* Do not signal wake up fd if there is no thread that wait's for it.
         * Otherwise poll will instantly wake up with wakeUpFd
         */
        wakeUpFd_.signal();
        alsaCaptureThread_.join();
    }
    close(bluetoothFifoFd_);
    bluetoothFifoFd_ = -1;
}

void SourceStreamSender::alsaCaptureThread()
{
    char streamBuffer[MAX_SOURCE_BUFFER_SIZE] = {0};
    reopenFifo();

    while (threadExists_) {
        memset(streamBuffer, 0x0, sizeof(streamBuffer[0]) * MAX_SOURCE_BUFFER_SIZE);

        /* This array contains 2 file descriptors:
         * At 0 index: it is a wakeUpFd. This fd notifies about BluetoothImpl destructor call, so when it get's
         * event poll wakes up and break 'while' cycle
         * At 1 index: it is a fd of FIFO. BluetoothImpl is a consumer of Bluetooth Source Stream. In current implementation
         * (linkplay SDK version 04), in libsoft_prep.so (it's used by audiod) there is a source code, that opens FIFO
         * and produce Stream to be send to SINK device.
         * NOTE: it's important to set up this array inside of while(...) cycle, because bluetoothFifoFd_
         * can change on each iteration
         */
        std::array<struct pollfd, 2> pollfds{{{wakeUpFd_.fd(), POLLIN | POLLPRI | POLLERR, 0},
                                              {bluetoothFifoFd_, POLLIN | POLLPRI | POLLERR, 0}}};

        int stream_len{0};

        for (auto& i : pollfds) {
            i.revents = 0;
        }

        int ret = poll(pollfds.data(), pollfds.size(), -1);
        if (ret < 0) {
            YIO_LOG_ERROR_EVENT("SourceStreamSender.PollFailed", "Poll FIFO's error: " + std::string(strerror(errno)));
            throw std::runtime_error("Poll FIFO's error: " + std::string(strerror(errno)));
        }
        if (pollfds[0].revents != 0) {
            /* Got Wake Up Fd event. Stop this thread */
            break;
        }

        if (pollfds[1].revents & (POLLERR)) {
            /* FIFO Error */
            YIO_LOG_ERROR_EVENT("SourceStreamSender.PollErr", "FIFO error: POLLERR");
            throw std::runtime_error("FIFO error: POLLERR");
        } else if (pollfds[1].revents & (POLLPRI | POLLIN)) {
            /* There is a data to read */
            stream_len = btreadstream(bluetoothFifoFd_, streamBuffer, MAX_SOURCE_BUFFER_SIZE);
            if (stream_len > 0) {
                sourceStreamSendBuffer(streamBuffer, stream_len);
            }
        }

        /* NOTE: It can be, that there is POLLHUP and POLLIN at the same time, so check pollhup last and do not skip data */
        if (pollfds[1].revents & POLLHUP) {
            /* Stream Source closed FIFO. Reopen FIFO and continue wait on poll */
            YIO_LOG_WARN("Bluetooth FIFO peer closed. Reopen FIFO file");
            reopenFifo();
            continue;
        }
    }
}

int SourceStreamSender::init()
{
    /* FIXME: Rework all samplerate */
    std::lock_guard<std::mutex> guard(pcmConfigMutex_);
    if (resampleInstance_ == nullptr) {
        pcmConfig_.sampling_freq = 44100;
        pcmConfig_.num_channel = 2;
        pcmConfig_.bit_per_sample = 16;

        resampleInstance_ = linkplay_resample_instance_create("44100:16:0:5:0.0");
        if (resampleInstance_ == nullptr) {
            throw std::runtime_error("Bluetooth: Can't init resample instance");
        }

        afData_.rate = 48000;
        afData_.nch = 2;
        afData_.bps = 2;

        linkplay_set_resample_format(&afData_, resampleInstance_);
        /* TODO: check return values */
    }

    return 0;
}

void SourceStreamSender::mix_areas_16(unsigned int size,
                                      volatile short* dst, short* src,
                                      volatile int* sum)
{
    int sample;
    int old_sample;

    while (size) {
        sample = *src;
        old_sample = *sum;
        if ((*dst) == 0) {
            sample -= old_sample;
        }
        *sum += sample;

        do {
            old_sample = *sum;
            if (old_sample > 0x7fff) {
                sample = 0x7fff;
            } else if (old_sample < -0x8000) {
                sample = -0x8000;
            } else {
                sample = old_sample;
            }
            *dst = sample;
        } while (*sum != old_sample);
        --size;
        ++src;
        ++dst;
        ++sum;
    }
}

void SourceStreamSender::reopenFifo()
{
    if (bluetoothFifoFd_ >= 0) {
        close(bluetoothFifoFd_);
    }
    bluetoothFifoFd_ = open(LINKPLAY_BlUETOOTH_AUDIO_FIFO, O_RDONLY | O_NONBLOCK);
    if (bluetoothFifoFd_ < 0) {
        std::stringstream ss;
        ss << "Can't open bluetooth FIFO: " << LINKPLAY_BlUETOOTH_AUDIO_FIFO << ". Error: " << strerror(errno);
        throw std::runtime_error(ss.str());
    }
}

int SourceStreamSender::btreadstream(int fd, char* dst_buf, int len)
{
    int read_len = read(fd, dst_buf, len);
    if (read_len <= 0) {
        /* Couldn't read from fifo: reopen it */
        reopenFifo();
    }

    return read_len;
}

int SourceStreamSender::a2dpmixstream(int fd, char* dst_buf, char* dst_sum)
{
    char buf[MAX_SOURCE_BUFFER_SIZE];

    ssize_t read_len = read(fd, buf, MAX_SOURCE_BUFFER_SIZE);
    if (read_len > 0) {
        mix_areas_16(read_len / 2, (volatile short*)dst_buf, (short*)buf, (int*)dst_sum);
    }
    return 0;
}

/*
 * Data passed to this function is 16bit signed, 2 channels
 */
int SourceStreamSender::sourceStreamSendBuffer(void* audio_buf, int length)
{
    std::lock_guard<std::mutex> guard(pcmConfigMutex_);
    if (resampleInstance_) {
        afData_.audio = audio_buf;
        afData_.len = length;
        af_data_t* outdata = linkPlay_resample(&afData_, resampleInstance_);
        if (outdata) {
            audio_buf = outdata->audio;
            length = outdata->len;
        }
    }
    return linkplay_bluetooth_source_send_stream(audio_buf, length);
}
