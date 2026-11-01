#pragma once

#include <yandex_io/libs/audio_player/base/stream_src.h>
#include <yandex_io/libs/threading/blocking_queue.h>

namespace quasar {
    class StreamAppSrc: public StreamSrc {
    public:
        StreamAppSrc()
            : queue_(MAX_QUEUE_SIZE)
        {
        }

        std::vector<uint8_t> pullData() override;
        std::string getName() const override;

        void pushData(std::vector<uint8_t> buffer);
        void setDataEnd();

    private:
        static constexpr int MAX_QUEUE_SIZE = 100;

        BlockingQueue<std::vector<uint8_t>> queue_;
        std::atomic<bool> endOfStream_{false};
    };
} // namespace quasar
