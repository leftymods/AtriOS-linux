#include <yandex_io/modules/audio_input/channels_splitter/channels_splitter.h>

#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <library/cpp/testing/unittest/registar.h>

#include <memory>
#include <sstream>
#include <string>

namespace {

    std::vector<uint8_t> prepareRawData(int sampleSize, int channelsCount, int frames) {
        // prepare frame
        std::vector<uint8_t> frame;
        frame.reserve(channelsCount * sampleSize);
        for (auto i = 0; i < channelsCount; ++i) {
            for (auto b = 0; b < sampleSize; ++b) {
                frame.push_back(i); // fill channel data with this channel number
            }
        }

        // insert frames
        std::vector<uint8_t> rawData;
        rawData.reserve(frames * channelsCount * sampleSize);
        for (auto i = 0; i < frames; ++i) {
            rawData.insert(rawData.end(), frame.begin(), frame.end());
        }
        return rawData;
    }

    std::vector<float> prepareRawDataFloatFromInt16(int channelsCount, int frames) {
        constexpr float sampleFloatScale = (1LL << (sizeof(int16_t) * 8 - 1));
        std::vector<float> res;
        res.resize(frames * channelsCount);
        for (int f = 0; f < frames; ++f) {
            for (int c = 0; c < channelsCount; ++c) {
                const float target = (c | (c << 8)) / sampleFloatScale;
                res[f * channelsCount + c] = target;
            }
        }
        return res;
    }

    void printVector(const std::vector<uint8_t>& raw) {
        std::stringstream ss;
        for (const auto& v : raw) {
            ss << int(v);
        }
        YIO_LOG_INFO(ss.str());
    }

} // namespace

Y_UNIT_TEST_SUITE_F(TestChannelsSplitter, QuasarUnitTestFixture) {
    Y_UNIT_TEST(testSplitterInt) {
        constexpr auto frames = 100;
        constexpr auto sampleSize = 2;
        constexpr auto channelsCount = 4;
        auto splitter = YandexIO::IChannelsSplitter::create(sampleSize, channelsCount);
        const auto rawData = prepareRawData(sampleSize, channelsCount, frames);
        YIO_LOG_INFO("Input data:");
        printVector(rawData);
        YIO_LOG_INFO("");
        {
            std::vector<uint8_t> result;
            // do not throw when no channels requested
            UNIT_ASSERT_NO_EXCEPTION(splitter->extractChannels(rawData, {}, result));
        }

        {
            // explicitly check that extracting all channels do not change data at all
            std::vector<uint8_t> result;
            splitter->extractChannels(rawData, {0, 1, 2, 3}, result);
            UNIT_ASSERT_VALUES_EQUAL(rawData.size(), result.size());
            UNIT_ASSERT_VALUES_EQUAL(result, rawData);
        }

        auto checkExtractChannels = [&splitter](const auto& rawData, std::vector<int> channels) {
            std::vector<uint8_t> result;
            UNIT_ASSERT_NO_EXCEPTION(splitter->extractChannels(rawData, channels, result));
            UNIT_ASSERT_VALUES_EQUAL(result.size(), channels.size() * frames * sampleSize);
            for (int i = 0; i < frames; ++i) {
                for (size_t c = 0; c < channels.size(); ++c) {
                    for (auto b = 0; b < sampleSize; ++b) {
                        // extracted channel should be equal to requested channel num
                        const auto shift = (i * channels.size() + c) * sampleSize + b;
                        const auto targetChannel = channels[c];
                        UNIT_ASSERT_VALUES_EQUAL(result[shift], targetChannel);
                    }
                }
            }
        };

        // Should work fine with any number of channels (less or equal to channels in rawData) and support reordering
        checkExtractChannels(rawData, {0, 1, 2, 3});
        checkExtractChannels(rawData, {1, 3, 0, 2});
        checkExtractChannels(rawData, {0});
        checkExtractChannels(rawData, {1});
        checkExtractChannels(rawData, {2});
        checkExtractChannels(rawData, {3});
        checkExtractChannels(rawData, {0, 1});
        checkExtractChannels(rawData, {2, 3});
        checkExtractChannels(rawData, {0, 3});
        checkExtractChannels(rawData, {1, 2});
        checkExtractChannels(rawData, {2, 1});
        checkExtractChannels(rawData, {1, 2, 3});
        checkExtractChannels(rawData, {2, 1, 0});
        checkExtractChannels(rawData, {});
        // check repeat same channel
        checkExtractChannels(rawData, {0, 0, 0, 0});
        checkExtractChannels(rawData, {0, 1, 0, 1});
        // check request channels more than input data have (with repeated channels)
        checkExtractChannels(rawData, {0, 0, 1, 1, 2, 2, 3, 3});
        checkExtractChannels(rawData, {0, 1, 0, 1, 2, 3, 2, 3});
    }

    Y_UNIT_TEST(testExtractChannelsSpanInt) {
        constexpr auto frames = 100;
        constexpr auto sampleSize = 2;
        constexpr auto channelsCount = 4;
        const auto rawDataBytes = prepareRawData(sampleSize, channelsCount, frames);
        YIO_LOG_INFO("Input data:");
        printVector(rawDataBytes);
        YIO_LOG_INFO("__end__");
        std::span<const int16_t> rawData(reinterpret_cast<const int16_t*>(rawDataBytes.data()), rawDataBytes.size() / 2);
        {
            std::vector<int16_t> result;
            // do not throw when no channels requested
            UNIT_ASSERT_NO_EXCEPTION(YandexIO::audio::extractChannels<int16_t>(rawData, channelsCount, {}, result));
        }
        auto checkExtractChannelsInt16 = [channelsCount](const auto& rawData, std::vector<int> channels) {
            std::vector<int16_t> result;
            result.resize(channels.size() * frames);
            UNIT_ASSERT_NO_EXCEPTION(YandexIO::audio::extractChannels<int16_t>(rawData, channelsCount, channels, result));
            for (int i = 0; i < frames; ++i) {
                for (size_t c = 0; c < channels.size(); ++c) {
                    // each byte of channel sample always equal to channel num
                    const auto targetChannel = channels[c];
                    const int16_t target = targetChannel | (targetChannel << 8);
                    const auto shift = (i * channels.size() + c);
                    UNIT_ASSERT_VALUES_EQUAL(result[shift], target);
                }
            }
        };

        checkExtractChannelsInt16(rawData, {0, 1, 2, 3});
        // Should work fine with any number of channels (less or equal to channels in rawData) and support reordering
        checkExtractChannelsInt16(rawData, {0, 1, 2, 3});
        checkExtractChannelsInt16(rawData, {1, 3, 0, 2});
        checkExtractChannelsInt16(rawData, {0});
        checkExtractChannelsInt16(rawData, {1});
        checkExtractChannelsInt16(rawData, {2});
        checkExtractChannelsInt16(rawData, {3});
        checkExtractChannelsInt16(rawData, {0, 1});
        checkExtractChannelsInt16(rawData, {2, 3});
        checkExtractChannelsInt16(rawData, {0, 3});
        checkExtractChannelsInt16(rawData, {1, 2});
        checkExtractChannelsInt16(rawData, {2, 1});
        checkExtractChannelsInt16(rawData, {1, 2, 3});
        checkExtractChannelsInt16(rawData, {2, 1, 0});
        checkExtractChannelsInt16(rawData, {});
        // check repeat same channel
        checkExtractChannelsInt16(rawData, {0, 0, 0, 0});
        checkExtractChannelsInt16(rawData, {0, 1, 0, 1});
        // check request channels more than input data have (with repeated channels)
        checkExtractChannelsInt16(rawData, {0, 0, 1, 1, 2, 2, 3, 3});
        checkExtractChannelsInt16(rawData, {0, 1, 0, 1, 2, 3, 2, 3});
    }

    Y_UNIT_TEST(testExtractChannelsFloat) {
        constexpr auto frames = 100;
        constexpr auto channelsCount = 4;
        const auto rawData = prepareRawDataFloatFromInt16(channelsCount, frames);
        {
            std::vector<float> result;
            // do not throw when no channels requested
            UNIT_ASSERT_NO_EXCEPTION(YandexIO::audio::extractChannels<float>(rawData, channelsCount, {}, result));
        }
        auto checkExtractChannelsFloat = [channelsCount](const auto& rawData, std::vector<int> channels) {
            std::vector<float> result;
            result.resize(channels.size() * frames);
            UNIT_ASSERT_NO_EXCEPTION(YandexIO::audio::extractChannels<float>(rawData, channelsCount, channels, result));
            for (int i = 0; i < frames; ++i) {
                for (size_t c = 0; c < channels.size(); ++c) {
                    // each byte of channel sample always equal to channel num
                    const auto targetChannel = channels[c];
                    constexpr float sampleFloatScale = (1LL << (sizeof(int16_t) * 8 - 1));
                    const float target = (targetChannel | (targetChannel << 8)) / sampleFloatScale;
                    const auto shift = (i * channels.size() + c);
                    UNIT_ASSERT_DOUBLES_EQUAL(result[shift], target, 0.0001);
                }
            }
        };

        checkExtractChannelsFloat(rawData, {0, 1, 2, 3});
        // Should work fine with any number of channels (less or equal to channels in rawData) and support reordering
        checkExtractChannelsFloat(rawData, {0, 1, 2, 3});
        checkExtractChannelsFloat(rawData, {1, 3, 0, 2});
        checkExtractChannelsFloat(rawData, {0});
        checkExtractChannelsFloat(rawData, {1});
        checkExtractChannelsFloat(rawData, {2});
        checkExtractChannelsFloat(rawData, {3});
        checkExtractChannelsFloat(rawData, {0, 1});
        checkExtractChannelsFloat(rawData, {2, 3});
        checkExtractChannelsFloat(rawData, {0, 3});
        checkExtractChannelsFloat(rawData, {1, 2});
        checkExtractChannelsFloat(rawData, {2, 1});
        checkExtractChannelsFloat(rawData, {1, 2, 3});
        checkExtractChannelsFloat(rawData, {2, 1, 0});
        checkExtractChannelsFloat(rawData, {});
        // check repeat same channel
        checkExtractChannelsFloat(rawData, {0, 0, 0, 0});
        checkExtractChannelsFloat(rawData, {0, 1, 0, 1});
        // check request channels more than input data have (with repeated channels)
        checkExtractChannelsFloat(rawData, {0, 0, 1, 1, 2, 2, 3, 3});
        checkExtractChannelsFloat(rawData, {0, 1, 0, 1, 2, 3, 2, 3});
    }

    Y_UNIT_TEST(testExtractChannelsToFloat) {
        constexpr auto frames = 100;
        constexpr auto sampleSize = 2;
        constexpr auto channelsCount = 4;
        const auto rawDataBytes = prepareRawData(sampleSize, channelsCount, frames);
        YIO_LOG_INFO("Input data:");
        printVector(rawDataBytes);
        YIO_LOG_INFO("__end__");
        std::span<const int16_t> rawData(reinterpret_cast<const int16_t*>(rawDataBytes.data()), rawDataBytes.size() / 2);
        {
            std::vector<float> result;
            // do not throw when no channels requested
            UNIT_ASSERT_NO_EXCEPTION(YandexIO::audio::extractChannelsToFloat<int16_t>(rawData, channelsCount, {}, result));
        }
        auto checkExtractChannelsFloat = [channelsCount](const auto& rawData, std::vector<int> channels) {
            std::vector<float> result;
            result.resize(channels.size() * frames);
            UNIT_ASSERT_NO_EXCEPTION(YandexIO::audio::extractChannelsToFloat<int16_t>(rawData, channelsCount, channels, result));
            for (int i = 0; i < frames; ++i) {
                for (size_t c = 0; c < channels.size(); ++c) {
                    // each byte of channel sample always equal to channel num
                    const auto targetChannel = channels[c];
                    constexpr float sampleFloatScale = (1LL << (sampleSize * 8 - 1));
                    const float target = (targetChannel | (targetChannel << 8)) / sampleFloatScale;
                    const auto shift = (i * channels.size() + c);
                    UNIT_ASSERT(result[shift] >= -1.0 && result[shift] <= 1.0);
                    UNIT_ASSERT_DOUBLES_EQUAL(result[shift], target, 0.0001);
                }
            }
        };

        checkExtractChannelsFloat(rawData, {0, 1, 2, 3});
        // Should work fine with any number of channels (less or equal to channels in rawData) and support reordering
        checkExtractChannelsFloat(rawData, {0, 1, 2, 3});
        checkExtractChannelsFloat(rawData, {1, 3, 0, 2});
        checkExtractChannelsFloat(rawData, {0});
        checkExtractChannelsFloat(rawData, {1});
        checkExtractChannelsFloat(rawData, {2});
        checkExtractChannelsFloat(rawData, {3});
        checkExtractChannelsFloat(rawData, {0, 1});
        checkExtractChannelsFloat(rawData, {2, 3});
        checkExtractChannelsFloat(rawData, {0, 3});
        checkExtractChannelsFloat(rawData, {1, 2});
        checkExtractChannelsFloat(rawData, {2, 1});
        checkExtractChannelsFloat(rawData, {1, 2, 3});
        checkExtractChannelsFloat(rawData, {2, 1, 0});
        checkExtractChannelsFloat(rawData, {});
        // check repeat same channel
        checkExtractChannelsFloat(rawData, {0, 0, 0, 0});
        checkExtractChannelsFloat(rawData, {0, 1, 0, 1});
        // check request channels more than input data have (with repeated channels)
        checkExtractChannelsFloat(rawData, {0, 0, 1, 1, 2, 2, 3, 3});
        checkExtractChannelsFloat(rawData, {0, 1, 0, 1, 2, 3, 2, 3});
    }

    Y_UNIT_TEST(testExtractChannelsToInt) {
        constexpr auto frames = 100;
        constexpr auto channelsCount = 4;
        const auto rawData = prepareRawDataFloatFromInt16(channelsCount, frames);
        {
            std::vector<int16_t> result;
            // do not throw when no channels requested
            UNIT_ASSERT_NO_EXCEPTION(YandexIO::audio::extractChannelsToInt<int16_t>(rawData, channelsCount, {}, result));
        }
        auto checkExtractChannelsToInt = [channelsCount](const auto& rawData, std::vector<int> channels) {
            std::vector<int16_t> result;
            result.resize(channels.size() * frames);
            UNIT_ASSERT_NO_EXCEPTION(YandexIO::audio::extractChannelsToInt<int16_t>(rawData, channelsCount, channels, result));
            for (int i = 0; i < frames; ++i) {
                for (size_t c = 0; c < channels.size(); ++c) {
                    // each byte of channel sample always equal to channel num
                    const auto targetChannel = channels[c];
                    const int16_t target = (targetChannel | (targetChannel << 8));
                    const auto shift = (i * channels.size() + c);
                    UNIT_ASSERT_VALUES_EQUAL(result[shift], target);
                }
            }
        };

        checkExtractChannelsToInt(rawData, {0, 1, 2, 3});
        // Should work fine with any number of channels (less or equal to channels in rawData) and support reordering
        checkExtractChannelsToInt(rawData, {0, 1, 2, 3});
        checkExtractChannelsToInt(rawData, {1, 3, 0, 2});
        checkExtractChannelsToInt(rawData, {0});
        checkExtractChannelsToInt(rawData, {1});
        checkExtractChannelsToInt(rawData, {2});
        checkExtractChannelsToInt(rawData, {3});
        checkExtractChannelsToInt(rawData, {0, 1});
        checkExtractChannelsToInt(rawData, {2, 3});
        checkExtractChannelsToInt(rawData, {0, 3});
        checkExtractChannelsToInt(rawData, {1, 2});
        checkExtractChannelsToInt(rawData, {2, 1});
        checkExtractChannelsToInt(rawData, {1, 2, 3});
        checkExtractChannelsToInt(rawData, {2, 1, 0});
        checkExtractChannelsToInt(rawData, {});
        // check repeat same channel
        checkExtractChannelsToInt(rawData, {0, 0, 0, 0});
        checkExtractChannelsToInt(rawData, {0, 1, 0, 1});
        // check request channels more than input data have (with repeated channels)
        checkExtractChannelsToInt(rawData, {0, 0, 1, 1, 2, 2, 3, 3});
        checkExtractChannelsToInt(rawData, {0, 1, 0, 1, 2, 3, 2, 3});
    }

} // suite
