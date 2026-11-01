#pragma once

#include <util/system/yassert.h>

#include <algorithm>
#include <concepts>
#include <memory>
#include <vector>

#include <span>

namespace YandexIO {

    /**
     * Channels splitter for int interleaved channels. Support s16le and s32le
     * Provide api to extract any number of channels and reordering
     */
    class IChannelsSplitter {
    public:
        static std::unique_ptr<IChannelsSplitter> create(int sampleSizeBytes, int totalChannels);

        virtual ~IChannelsSplitter() = default;
        /**
         * @brief extract requested channels from input data
         * @param[in] raw - input data
         * @param[in] channels - requested channels to extract. Channels can be in any order. Same channel can be used few times.
         *                       Result will contain data in order provided by channels vector
         * @param[out] result - reference to result value (will be resized). Result will contain interleaved data of requested channel
         * @note extractChannels do not check that Channels requested in channels vector are valid (channels[i] < totalChannels);
         */
        virtual void extractChannels(const std::vector<uint8_t>& raw, const std::vector<int>& channels, std::vector<uint8_t>& result) = 0;
    };

    namespace audio {
        // concept for Int/Float audio data
        template <typename T>
        concept AudioData = std::is_integral_v<T> || std::is_floating_point_v<T>;

        template <AudioData T>
        void extractChannels(std::span<const T> in, int inChannelsCount, std::span<const int> channels, std::span<T> out) {
            const size_t frames = in.size() / inChannelsCount;
            const size_t requestedChannelsCount = channels.size();
            Y_VERIFY(out.size() >= frames * requestedChannelsCount);
            for (size_t i = 0; i < frames; ++i) {
                const auto outFrameShift = i * requestedChannelsCount;
                const auto inFrameShift = i * inChannelsCount;
                for (size_t c = 0; c < requestedChannelsCount; ++c) {
                    const auto targetChannel = channels[c];
                    out[outFrameShift + c] = in[inFrameShift + targetChannel];
                }
            }
        }

        template <std::integral T>
        void extractChannelsToFloat(std::span<const T> in, int inChannelsCount, std::span<const int> channels, std::span<float> out) {
            const size_t frames = in.size() / inChannelsCount;
            const size_t requestedChannelsCount = channels.size();
            Y_VERIFY(out.size() >= frames * requestedChannelsCount);
            constexpr float sampleFloatScale = (1LL << (sizeof(T) * 8 - 1));
            for (size_t i = 0; i < frames; ++i) {
                const auto outFrameShift = i * requestedChannelsCount;
                const auto inFrameShift = i * inChannelsCount;
                for (size_t c = 0; c < requestedChannelsCount; ++c) {
                    const auto targetChannel = channels[c];
                    out[outFrameShift + c] = in[inFrameShift + targetChannel] / sampleFloatScale;
                }
            }
        }

        template <std::integral T>
        void extractChannelsToInt(std::span<const float> in, int inChannelsCount, std::span<const int> channels, std::span<T> out) {
            const size_t frames = in.size() / inChannelsCount;
            const size_t requestedChannelsCount = channels.size();
            Y_VERIFY(out.size() >= frames * requestedChannelsCount);
            constexpr float sampleFloatScale = (1LL << (sizeof(T) * 8 - 1));
            for (size_t i = 0; i < frames; ++i) {
                const auto outFrameShift = i * requestedChannelsCount;
                const auto inFrameShift = i * inChannelsCount;
                for (size_t c = 0; c < requestedChannelsCount; ++c) {
                    const auto targetChannel = channels[c];
                    const float sample = in[inFrameShift + targetChannel] * sampleFloatScale;
                    out[outFrameShift + c] = static_cast<T>(std::clamp(sample, -sampleFloatScale, sampleFloatScale - 1));
                }
            }
        }

    } // namespace audio
} /* namespace YandexIO */
