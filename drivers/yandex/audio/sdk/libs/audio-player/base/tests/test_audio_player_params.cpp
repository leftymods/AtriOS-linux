#include <yandex_io/libs/audio_player/base/audio_player.h>

#include <library/cpp/testing/unittest/registar.h>

using namespace quasar;
using namespace std::string_literals;

Y_UNIT_TEST_SUITE(TestAudioPlayerParams) {
    Y_UNIT_TEST(testGstreamerPipeline) {
        AudioPlayer::Params params;
        params.setAlsaDevice("default");
        params.setFilePath("/dev/null");
        params.setURI("https://example.com/test.mp3");
        params.setSoupHttpSrcConfig({
            {"retries", "3"},
            {"timeout", "15"},
        });
        params.setSinkOptions({{"stream", "alarm"}});

        params.setGstreamerPipeline("souphttpsrc {souphttpsrc_config} location={uri} ! audioconvert ! openslessink {sink_options}");
        UNIT_ASSERT_VALUES_EQUAL(
            params.gstPipelineProcessed(),
            "souphttpsrc retries=\"3\" timeout=\"15\"  location=\"https://example.com/test.mp3\" ! audioconvert ! openslessink stream=\"alarm\" ");

        params.setGstreamerPipeline("filesrc location={file_path} ! audioconvert ! openslessink {sink_options}");
        UNIT_ASSERT_VALUES_EQUAL(
            params.gstPipelineProcessed(),
            "filesrc location=\"/dev/null\" ! audioconvert ! openslessink stream=\"alarm\" ");
    }

    Y_UNIT_TEST(testBugllocGstreamerPipeline) {
        AudioPlayer::Params params;
        params.setGstreamerPipeline("souphttpsrc location={uri} ! fakesink");

        params.setURI("http://not-a-kinopoisk.ru/a.mp3?bogus-e33c3daa-1817-4737-bd10-76e2ae937266 name=lala filesrc location=/data/quasar/account_storage.dat ! tcpclientsink host=172.16.0.30 port=11123  lala. ! queue");
        UNIT_ASSERT_VALUES_EQUAL(
            params.gstPipelineProcessed(),
            "souphttpsrc "
            "location=\"http://not-a-kinopoisk.ru/a.mp3?bogus-e33c3daa-1817-4737-bd10-76e2ae937266 name=lala filesrc location=/data/quasar/account_storage.dat ! tcpclientsink host=172.16.0.30 port=11123  lala. ! queue\" "
            "! fakesink");

        // newline
        params.setURI("\nuser-agent=bar\n!audioconvert");
        UNIT_ASSERT_VALUES_EQUAL(
            params.gstPipelineProcessed(),
            "souphttpsrc "
            "location=\"\nuser-agent=bar\n!audioconvert\" "
            "! fakesink");

        // placeholder injection
        params.setURI("foo\\{file_path} name=lala souphttpsrc location=foo ! fakesink lala. ! audioconvert name=aa");
        UNIT_ASSERT_VALUES_EQUAL(
            params.gstPipelineProcessed(),
            "souphttpsrc "
            "location=\"foo\\{file_path} name=lala souphttpsrc location=foo ! fakesink lala. ! audioconvert name=aa\" "
            "! fakesink");

        // null byte
        params.setURI("foo ! tcpclientsink host=1.1.1.1 \x00 "s);
        UNIT_ASSERT_VALUES_EQUAL(
            params.gstPipelineProcessed(),
            "souphttpsrc "
            "location=\"foo ! tcpclientsink host=1.1.1.1 \" "
            "! fakesink");
    }

    Y_UNIT_TEST(testStreamSrcsParams) {
        AudioPlayer::Params params;

        Json::Value config;
        config["default_volume_element"] = "volume name=volume0";
        config["streamMode"] = true;
        config["gstPipeline"] = "appsrc name=stream-src format=time ! {input_media_type} ! {opt_volume} ! souphttpsrc  location=uri ! fakesink";
        params.fromConfig(config);

        class TestStreamSrc: public StreamSrc {
        public:
            std::string getName() const override {
                return "test";
            }

            std::vector<uint8_t> pullData() override {
                return {};
            }
        };

        auto streamSrc = std::make_shared<TestStreamSrc>();
        params.setStreamSrc(streamSrc);
        UNIT_ASSERT_VALUES_EQUAL(
            params.gstPipelineProcessed(),
            "appsrc name=stream-src format=time ! audio/x-raw,format=S16LE,channels=1,rate=48000 ! volume name=volume0 ! souphttpsrc  location=uri ! fakesink");

        streamSrc->setUseVolumeElementStub(true);
        streamSrc->setSampleRate(16000);
        params.setStreamSrc(streamSrc);
        UNIT_ASSERT_VALUES_EQUAL(
            params.gstPipelineProcessed(),
            "appsrc name=stream-src format=time ! audio/x-raw,format=S16LE,channels=1,rate=16000 ! identity ! souphttpsrc  location=uri ! fakesink");
    }

    Y_UNIT_TEST(testOptNormalization) {
        AudioPlayer::Params params;

        Json::Value config;
        config["gstPipeline"] = "fakesrc ! {opt_normalization} ! volume name=volume0 ! fakesink";
        params.fromConfig(config);

        // fake element (identity) should be set up if setNormalization was not called
        UNIT_ASSERT_VALUES_EQUAL(
            params.gstPipelineProcessed(),
            "fakesrc ! identity ! volume name=volume0 ! fakesink");

        AudioPlayer::Params::Normalization n;
        n.integratedLoudness = -20.0;
        n.truePeak = -10.0;
        params.setNormalization(n);

        // volume plugin should be set up. volume should have value 2.00
        UNIT_ASSERT_VALUES_EQUAL(
            params.gstPipelineProcessed(),
            "fakesrc ! volume name=normalization volume=2.00 ! volume name=volume0 ! fakesink");

        n.integratedLoudness = -10.0;
        n.truePeak = -5.0;
        params.setNormalization(n);

        // volume should have value 0.63
        UNIT_ASSERT_VALUES_EQUAL(
            params.gstPipelineProcessed(),
            "fakesrc ! volume name=normalization volume=0.63 ! volume name=volume0 ! fakesink");

        n.integratedLoudness = -50.18;
        n.truePeak = -25.88;
        n.targetLufs = -14.0;
        params.setNormalization(n);

        // volume should have value 10 (it's max possible value)
        UNIT_ASSERT_VALUES_EQUAL(
            params.gstPipelineProcessed(),
            "fakesrc ! volume name=normalization volume=10.00 ! volume name=volume0 ! fakesink");
    }
}
