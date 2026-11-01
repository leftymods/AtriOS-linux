#include <smart_devices/platforms/yandexstation_2/platformd/audio_device/audio_readers.h>

#include <yandex_io/libs/telemetry/null/null_metrica.h>
#include <yandex_io/sdk/audio_source/feedback_source.h>

#include <library/cpp/testing/unittest/registar.h>

#include <memory>
#include <vector>

namespace quasar {

    const int MIC_CHANNELS = 8;
    const int MIC_BUFFER_SIZE = MIC_CHANNELS * 1024;
    const int CHANNELS = 2;
    const int BUFFER_SIZE = CHANNELS * 1024;

    Y_UNIT_TEST_SUITE(TestAudioReaders) {
        Y_UNIT_TEST(TestNoHwSync) {
            YandexIO::AudioDeviceStats audioDeviceStats{std::make_shared<NullMetrica>()};
            auto logSession = audioDeviceStats.createChunkSession(0);

            const bool useHwSync = false;

            AudioReaders audioReaders(
                {std::make_unique<ZerosAudioReader>(CHANNELS, BUFFER_SIZE),
                 std::make_unique<ZerosAudioReader>(MIC_CHANNELS, MIC_BUFFER_SIZE),
                 std::make_unique<ZerosAudioReader>(CHANNELS, BUFFER_SIZE),
                 std::make_unique<ZerosAudioReader>(CHANNELS, BUFFER_SIZE)},
                useHwSync,
                YandexIO::FeedbackSource::HW);

            AudioBuffer inBufSpk;
            AudioBuffer inBufLb;
            AudioBuffer inBufMic;
            AudioBuffer inBufHdmiLb;
            audioReaders.read(inBufMic, inBufSpk, inBufLb, inBufHdmiLb, logSession);
            auto downsampledSpk = audioReaders.downsampleSpk(inBufSpk);
            auto downsampledLb = audioReaders.downsampleLb(inBufLb);
            auto downsampledMic = audioReaders.downsampleMic(inBufMic);
            auto downsampledHdmiLb = audioReaders.downsampleHdmi(inBufHdmiLb);
            UNIT_ASSERT_EQUAL(downsampledSpk.size(), BUFFER_SIZE);
            UNIT_ASSERT_EQUAL(downsampledLb.size(), BUFFER_SIZE);
            UNIT_ASSERT_EQUAL(downsampledMic.size(), MIC_BUFFER_SIZE);
            UNIT_ASSERT_EQUAL(downsampledHdmiLb.size(), BUFFER_SIZE);
        }

        Y_UNIT_TEST(TestHwSync) {
            YandexIO::AudioDeviceStats audioDeviceStats{std::make_shared<NullMetrica>()};
            auto logSession = audioDeviceStats.createChunkSession(0);

            const bool useHwSync = true;

            AudioReaders audioReaders(
                {std::make_unique<ZerosAudioReader>(CHANNELS, BUFFER_SIZE),
                 std::make_unique<ZerosAudioReader>(MIC_CHANNELS, MIC_BUFFER_SIZE),
                 std::make_unique<ZerosAudioReader>(CHANNELS, BUFFER_SIZE),
                 std::make_unique<ZerosAudioReader>(CHANNELS, BUFFER_SIZE)},
                useHwSync,
                YandexIO::FeedbackSource::HW);

            AudioBuffer inBufSpk;
            AudioBuffer inBufLb;
            AudioBuffer inBufMic;
            AudioBuffer inBufHdmiLb;
            audioReaders.read(inBufMic, inBufSpk, inBufLb, inBufHdmiLb, logSession);
            auto downsampledSpk = audioReaders.downsampleSpk(inBufSpk);
            auto downsampledLb = audioReaders.downsampleLb(inBufLb);
            auto downsampledMic = audioReaders.downsampleMic(inBufMic);
            auto downsampledHdmiLb = audioReaders.downsampleHdmi(inBufHdmiLb);
            UNIT_ASSERT_EQUAL(downsampledSpk.size(), BUFFER_SIZE);
            UNIT_ASSERT_EQUAL(downsampledLb.size(), BUFFER_SIZE);
            UNIT_ASSERT_EQUAL(downsampledMic.size(), MIC_BUFFER_SIZE);
            UNIT_ASSERT_EQUAL(downsampledHdmiLb.size(), BUFFER_SIZE);
        }
    }
} // namespace quasar
