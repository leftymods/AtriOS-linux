#pragma once

#include <memory>
#include <string>

namespace quasar {

    class StreamSrc {
    public:
        using Data = std::vector<uint8_t>;

        virtual ~StreamSrc() = default;

        virtual std::string getName() const = 0;

        std::string getMediaTypeString() const;

        virtual Data pullData() = 0;

        void setSampleRate(int sampleRate);

        void setUseVolumeElementStub(bool useStub);

        bool useVolumeElementStub() const;

        int getSampleSize() const;
        int getSampleRate() const;
        int getChannelsCount() const;

    private:
        int sampleRate_{48000};
        const int channels_{1};
        const std::string format_{"S16LE"};
        const int sampleSize_ = 2;
        bool useVolumeElementStub_{false};
    };

    using StreamSrcPtr = std::shared_ptr<StreamSrc>;

} // namespace quasar
