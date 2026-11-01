#include "channels_splitter.h"

#include <stdexcept>
#include <string>

namespace {
    template <typename Sample>
    class ChannelsSplitterImpl: public YandexIO::IChannelsSplitter {
    public:
        ChannelsSplitterImpl(int totalChannels)
            : totalChannels_(totalChannels)
        {
        }

        void extractChannels(const std::vector<uint8_t>& rawData, const std::vector<int>& channels, std::vector<uint8_t>& result) override {
            const int frameSizeInBytes = totalChannels_ * sizeof(Sample);
            const size_t frames = rawData.size() / frameSizeInBytes;
            const size_t resultChannels = channels.size();
            result.resize(frames * resultChannels * sizeof(Sample));
            std::span<const Sample> in(reinterpret_cast<const Sample*>(rawData.data()), rawData.size() / sizeof(Sample));
            std::span<Sample> out(reinterpret_cast<Sample*>(result.data()), result.size() / sizeof(Sample));
            YandexIO::audio::extractChannels<Sample>(in, totalChannels_, channels, out);
        }

    private:
        const int totalChannels_;
    };

} // namespace

namespace YandexIO {

    std::unique_ptr<IChannelsSplitter> IChannelsSplitter::create(int sampleSizeBytes, int totalChannels) {
        if (sampleSizeBytes == 2) {
            return std::make_unique<ChannelsSplitterImpl<int16_t>>(totalChannels);
        } else if (sampleSizeBytes == 4) {
            return std::make_unique<ChannelsSplitterImpl<int32_t>>(totalChannels);
        }
        throw std::runtime_error("Can't create ChannelSplitter for sampleSizeBytes = " + std::to_string(sampleSizeBytes));
    }

} // namespace YandexIO
