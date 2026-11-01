#include <yandex_io/libs/audio_player/gstreamer/gstreamer.h>
#include <yandex_io/libs/audio_player/gstreamer/gstreamer_audio_player.h>

#include <yandex_io/libs/logging/logging.h>

#include <yandex_io/services/mediad/audioclient/stream_app_src.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <library/cpp/testing/unittest/registar.h>

#include <atomic>
#include <functional>
#include <future>
#include <thread>
#include <vector>

using namespace quasar;
using namespace quasar::gstreamer;

namespace {

    class ProgressListener: public AudioPlayer::SimpleListener {
    public:
        using OnProgress = std::function<void(int, int)>;
        explicit ProgressListener(OnProgress onProgressCb)
            : onProgress_(std::move(onProgressCb))
        {
        }
        void onProgress(int position, int duration) override {
            onProgress_(position, duration);
        }

    private:
        OnProgress onProgress_;
    };

    std::vector<uint8_t> generateSilence(int rate, int sampleSize, int ms) {
        const size_t dataInMs = rate * sampleSize * ms / 1000;
        constexpr uint8_t silenceValue = 0;
        return std::vector<uint8_t>(dataInMs, silenceValue);
    }

} // namespace

Y_UNIT_TEST_SUITE_F(GstreamerAudioPlayerTest, QuasarUnitTestFixture) {
    Y_UNIT_TEST(testDurationOnMp3) {
        GstreamerAudioPlayerFactory facotry(ensureGstreamerInitialized());

        AudioPlayer::Params params;
        params.setGstreamerPipeline("filesrc location={file_path} ! decodebin ! audioconvert ! audioresample ! audio/x-raw,rate=48000,channels=2,format=S16LE ! fakesink sync=true");
        params.setFilePath(ArcadiaSourceRoot() + "/yandex_io/libs/audio_player/gstreamer/tests/testdata/test.mp3");
        const auto player = facotry.createPlayer(params);
        std::promise<void> progressPromise;
        player->addListener(std::make_shared<ProgressListener>([&progressPromise, lastProgress{-1}, once{true}](int progress, int duration) mutable {
            YIO_LOG_INFO("Progress: " << progress << ", duration: " << duration);
            UNIT_ASSERT_VALUES_EQUAL(duration, 29); // test.mp3 is a copy of enter_configure.mp3. It's duration is 29sec
            UNIT_ASSERT(progress >= 0);
            UNIT_ASSERT(progress >= lastProgress);
            lastProgress = progress;
            if (progress >= 3 && once) {
                once = false;
                // test is complete
                progressPromise.set_value();
            }
        }));
        player->playAsync();

        progressPromise.get_future().get();
    }

    Y_UNIT_TEST(testNoDurationForAppSrc) {
        constexpr int sampleRate = 16000;
        GstreamerAudioPlayerFactory facotry(ensureGstreamerInitialized());
        AudioPlayer::Params params;
        params.setGstreamerPipeline("appsrc name=stream-src format=time ! {input_media_type} ! volume name=volume0 ! audioresample ! fakesink sync=true");
        params.setIsStreamMode(true);
        const auto streamSrc = std::make_shared<StreamAppSrc>();
        streamSrc->setSampleRate(sampleRate);
        params.setStreamSrc(streamSrc);

        const auto player = facotry.createPlayer(params);
        std::promise<void> progressPromise;
        player->addListener(std::make_shared<ProgressListener>([&progressPromise, lastProgress{-1}, once{true}](int progress, int duration) mutable {
            YIO_LOG_INFO("Progress: " << progress << ", duration: " << duration);
            UNIT_ASSERT_VALUES_EQUAL(duration, 0); // duration should be zero for live streams
            UNIT_ASSERT(progress >= 0);
            UNIT_ASSERT(progress >= lastProgress);
            lastProgress = progress;
            if (progress >= 3 && once) {
                once = false;
                // test is complete
                progressPromise.set_value();
            }
        }));
        player->playAsync();

        std::atomic_bool stopped_{false};
        auto dataFeedThread_ = std::thread([&]() {
            const auto silence100Ms = generateSilence(sampleRate, 2 /*s16le*/, 100 /*ms*/);
            YIO_LOG_INFO("Start Data Feed");
            while (!stopped_.load()) {
                streamSrc->pushData(silence100Ms);
                // emulate streaming
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

        progressPromise.get_future().get();

        stopped_.store(true);
        dataFeedThread_.join();
        YIO_LOG_INFO("Set appsrc end");
        streamSrc->setDataEnd();
    }
}
