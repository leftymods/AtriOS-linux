#include <yandex_io/libs/audio_player/base/utils.h>

#include <library/cpp/testing/unittest/registar.h>

using namespace quasar;
using namespace std::string_literals;

Y_UNIT_TEST_SUITE(TestUtils) {
    Y_UNIT_TEST(testEscapeGstString) {
        UNIT_ASSERT_VALUES_EQUAL(escapeGstString("foo"), "\"foo\"");
        UNIT_ASSERT_VALUES_EQUAL(escapeGstString("foo bar"), "\"foo bar\"");
        UNIT_ASSERT_VALUES_EQUAL(escapeGstString("foo\nbar"), "\"foo\nbar\"");
        UNIT_ASSERT_VALUES_EQUAL(escapeGstString("foo\"bar"), "\"foo\\\"bar\"");
        UNIT_ASSERT_VALUES_EQUAL(escapeGstString("foo\0bar"s), "\"foo\"");
    }

    Y_UNIT_TEST(testSerializeGstOptions) {
        UNIT_ASSERT_VALUES_EQUAL(
            serializeGstOptions({
                {"retries", "3"},
                {"timeout", "15"},
            }),
            "retries=\"3\" timeout=\"15\" ");

        UNIT_ASSERT_VALUES_EQUAL(
            serializeGstOptions({
                {"o_:-%", "value"},
                {"option", "with space"},
            }),
            "o_:-%=\"value\" option=\"with space\" ");

        UNIT_ASSERT_EXCEPTION(
            serializeGstOptions({
                {"bad option", "value"},
            }),
            std::runtime_error);
    }

    Y_UNIT_TEST(testcalcGstNormalizationVolume) {
        using Normalization = AudioPlayer::Params::Normalization;
        // test values with default target_lufs == -14.0
        UNIT_ASSERT_DOUBLES_EQUAL(1.99, calcGstNormalizationVolume(Normalization{.truePeak = -10.0, .integratedLoudness = -20.0}), 0.1);
        UNIT_ASSERT_DOUBLES_EQUAL(1.58, calcGstNormalizationVolume(Normalization{.truePeak = -5.0, .integratedLoudness = -20.0}), 0.1);
        UNIT_ASSERT_DOUBLES_EQUAL(0.63, calcGstNormalizationVolume(Normalization{.truePeak = -5.0, .integratedLoudness = -10.0}), 0.1);
        UNIT_ASSERT_DOUBLES_EQUAL(1.0, calcGstNormalizationVolume(Normalization{.truePeak = -0.5, .integratedLoudness = -20.0}), 0.1);
        // test values with custom target_lufs
        UNIT_ASSERT_DOUBLES_EQUAL(2.51, calcGstNormalizationVolume(Normalization{.truePeak = -10.0, .integratedLoudness = -20.0, .targetLufs = -12.0}), 0.1);
        UNIT_ASSERT_DOUBLES_EQUAL(1.58, calcGstNormalizationVolume(Normalization{.truePeak = -5.0, .integratedLoudness = -20.0, .targetLufs = -12.0}), 0.1);
        UNIT_ASSERT_DOUBLES_EQUAL(0.74, calcGstNormalizationVolume(Normalization{.truePeak = -5.0, .integratedLoudness = -10.0, .targetLufs = -12.0}), 0.1);
        UNIT_ASSERT_DOUBLES_EQUAL(1.0, calcGstNormalizationVolume(Normalization{.truePeak = -0.5, .integratedLoudness = -20.0, .targetLufs = -12.0}), 0.1);

        // ensure that x10 is max (even when target db is 20+)
        UNIT_ASSERT_DOUBLES_EQUAL(10.0, calcGstNormalizationVolume(Normalization{.truePeak = -25.88, .integratedLoudness = -50.18, .targetLufs = -14.0}), 0.1);
    }

    Y_UNIT_TEST(testCalcNormalizationDb) {
        using Normalization = AudioPlayer::Params::Normalization;
        // test values with default target_lufs == -14.0
        UNIT_ASSERT_DOUBLES_EQUAL(6.0, calcNormalizationDb(Normalization{.truePeak = -10.0, .integratedLoudness = -20.0}), 0.1);
        UNIT_ASSERT_DOUBLES_EQUAL(4.0, calcNormalizationDb(Normalization{.truePeak = -5.0, .integratedLoudness = -20.0}), 0.1);
        UNIT_ASSERT_DOUBLES_EQUAL(-4.0, calcNormalizationDb(Normalization{.truePeak = -5.0, .integratedLoudness = -10.0}), 0.1);
        UNIT_ASSERT_DOUBLES_EQUAL(0.0, calcNormalizationDb(Normalization{.truePeak = -0.5, .integratedLoudness = -20.0}), 0.1);
        // test values with custom target_lufs
        UNIT_ASSERT_DOUBLES_EQUAL(8.0, calcNormalizationDb(Normalization{.truePeak = -10.0, .integratedLoudness = -20.0, .targetLufs = -12.0}), 0.1);
        UNIT_ASSERT_DOUBLES_EQUAL(4.0, calcNormalizationDb(Normalization{.truePeak = -5.0, .integratedLoudness = -20.0, .targetLufs = -12.0}), 0.1);
        UNIT_ASSERT_DOUBLES_EQUAL(-2.0, calcNormalizationDb(Normalization{.truePeak = -5.0, .integratedLoudness = -10.0, .targetLufs = -12.0}), 0.1);
        UNIT_ASSERT_DOUBLES_EQUAL(0.0, calcNormalizationDb(Normalization{.truePeak = -0.5, .integratedLoudness = -20.0, .targetLufs = -12.0}), 0.1);

        UNIT_ASSERT_DOUBLES_EQUAL(24.88, calcNormalizationDb(Normalization{.truePeak = -25.88, .integratedLoudness = -50.18, .targetLufs = -14.0}), 0.1);
    }
}
