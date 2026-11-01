#pragma once

#include <smart_devices/third_party/linkplay_bluetooth/linkplay_bluetooth_interface.h>
#include <smart_devices/third_party/linkplay/linkplay_resample_interface.h>

#include <yandex_io/libs/threading/wakeup_fd.h>

#include <atomic>
#include <mutex>
#include <thread>

class SourceStreamSender {
public:
    SourceStreamSender();
    ~SourceStreamSender();

    void runCaptureThread();
    void stopCaptureThread();
    tLINKPLAY_AV_MEDIA_FEED_CFG_PCM getPcmConfig() const;

private:
    tLINKPLAY_AV_MEDIA_FEED_CFG_PCM pcmConfig_;
    af_instance_t* resampleInstance_ = nullptr;
    af_data_t afData_;

    /**
     * @brief This fifo is used to get mixed and resampled audio source for stream out.
     *        SourceStreamSender is a consumer for this FIFO; The producer is libsoft_prep
     *        (which is used by audiod). The producer get resmapled and mixed signal from
     *        ALSA device and send through FIFO. It's seems like, that linkplay decided to
     *        use same signal which they are using for WQE/AEC
     */
    static constexpr const char* LINKPLAY_BlUETOOTH_AUDIO_FIFO = "/tmp/loop_capture_pcm";

    int bluetoothFifoFd_{-1};
    void reopenFifo();

    static constexpr int MAX_SOURCE_BUFFER_SIZE = 2048;

    int init();
    int btreadstream(int fd, char* dst_buf, int len);

    int sourceStreamSendBuffer(void* audio_buf, int length);
    static void mix_areas_16(unsigned int size, volatile short* dst, short* src, volatile int* sum);
    static int a2dpmixstream(int fd, char* dst_buf, char* dst_sum);

    void alsaCaptureThread();

    std::thread alsaCaptureThread_;
    mutable std::mutex threadMutex_;
    mutable std::mutex pcmConfigMutex_;

    std::atomic_bool threadExists_;
    quasar::WakeupFd wakeUpFd_;
};
