#include "interleaver.h"

#include <util/system/compiler.h>
#include <util/system/yassert.h>

#include <string>

namespace quasar {
    void Interleaver::add(const std::vector<float>& channels, int count) {
        if (count == 0) {
            return;
        }
        if (Y_UNLIKELY(count < 0)) {
            throw std::runtime_error("Negative amount of channels: " + std::to_string(count));
        }
        if (Y_UNLIKELY(channels.size() % count != 0)) {
            throw std::runtime_error("Incorrect size of channels vector (" + std::to_string(channels.size()) + ") for " + std::to_string(count) + " channels");
        }

        if (!channelsSequence_.empty()) {
            int frontChannelSize = channelsSequence_.front().first->size() / channelsSequence_.front().second;
            int currentChannelSize = channels.size() / count;
            if (Y_UNLIKELY(frontChannelSize != currentChannelSize)) {
                throw std::runtime_error("Incorrect size of different sources: " + std::to_string(frontChannelSize) + " != " + std::to_string(currentChannelSize));
            }
        }

        channelsSequence_.push_back({&channels, count});
    }

    void Interleaver::merge(std::vector<float>& result) const {
        size_t totalSamples = 0u;
        size_t totalChannels = 0u;
        for (const auto& ch : channelsSequence_) {
            totalSamples += ch.first->size();
            totalChannels += ch.second;
        }
        Y_VERIFY(totalSamples % totalChannels == 0);
        const size_t frameCount = totalSamples / totalChannels;

        result.reserve(totalSamples);
        result.resize(0);
        auto resultBackInterter = std::back_inserter(result);
        for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            for (const auto& chData : channelsSequence_) {
                auto channelFrameBegin = chData.first->begin() + frameIndex * chData.second;
                std::copy(channelFrameBegin, channelFrameBegin + chData.second, resultBackInterter);
            }
        }
    }
} // namespace quasar
