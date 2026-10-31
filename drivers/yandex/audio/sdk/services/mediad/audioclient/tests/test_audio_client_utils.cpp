#include <yandex_io/services/mediad/audioclient/audio_client_utils.h>

#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <library/cpp/testing/unittest/registar.h>

using namespace quasar;

Y_UNIT_TEST_SUITE_F(TestAudioClientUtils, QuasarUnitTestFixture) {
    Y_UNIT_TEST(setVsidToUrl) {
        const std::string vsid = "ialak21suzo4kwfdydf4r92pahfer9waki040hcuxhehxSTMx0000x1630063895";

        {
            // test url without query params
            const auto url = "https://strm.yandex.ru/music/music-strm-jsons/170252.bd1c6e46/kaltura/38426195.5.320/1630064015/1630046692/f52a7ae429f725afeb4bace544647cf2afcd7077a68b6f4b04c7a1e8e5b4d43a/master.m3u8";
            const auto result = AudioClientUtils::setVSIDToUrlParams(url, vsid);
            const auto expect = "https://strm.yandex.ru/music/music-strm-jsons/170252.bd1c6e46/kaltura/38426195.5.320/1630064015/1630046692/f52a7ae429f725afeb4bace544647cf2afcd7077a68b6f4b04c7a1e8e5b4d43a/master.m3u8?vsid=ialak21suzo4kwfdydf4r92pahfer9waki040hcuxhehxSTMx0000x1630063895";
            UNIT_ASSERT_VALUES_EQUAL(result, expect);
        }

        {
            // test url with query params
            const auto url = "https://strm.yandex.ru/music/music-strm-jsons/170252.bd1c6e46/kaltura/38426195.5.320/1630064015/1630046692/f52a7ae429f725afeb4bace544647cf2afcd7077a68b6f4b04c7a1e8e5b4d43a/master.m3u8?abc_id=94&from=ya-music";
            const auto result = AudioClientUtils::setVSIDToUrlParams(url, vsid);
            const auto expect = "https://strm.yandex.ru/music/music-strm-jsons/170252.bd1c6e46/kaltura/38426195.5.320/1630064015/1630046692/f52a7ae429f725afeb4bace544647cf2afcd7077a68b6f4b04c7a1e8e5b4d43a/master.m3u8?abc_id=94&from=ya-music&vsid=ialak21suzo4kwfdydf4r92pahfer9waki040hcuxhehxSTMx0000x1630063895";
            UNIT_ASSERT_VALUES_EQUAL(result, expect);
        }

        {
            // test url with query params and VSID. vsid should be replaced
            const auto url = "https://strm.yandex.ru/music/music-strm-jsons/170252.bd1c6e46/kaltura/38426195.5.320/1630064015/1630046692/f52a7ae429f725afeb4bace544647cf2afcd7077a68b6f4b04c7a1e8e5b4d43a/master.m3u8?abc_id=94&from=ya-music&vsid=MTFaq59GKdD4E4s2FKdUapHHZZffjwa4pc19Uw5MmZMXxSTMx0000x1630063895";
            const auto result = AudioClientUtils::setVSIDToUrlParams(url, vsid);
            const auto expect = "https://strm.yandex.ru/music/music-strm-jsons/170252.bd1c6e46/kaltura/38426195.5.320/1630064015/1630046692/f52a7ae429f725afeb4bace544647cf2afcd7077a68b6f4b04c7a1e8e5b4d43a/master.m3u8?abc_id=94&from=ya-music&vsid=ialak21suzo4kwfdydf4r92pahfer9waki040hcuxhehxSTMx0000x1630063895";
            UNIT_ASSERT_VALUES_EQUAL(result, expect);
        }

        {
            // test url with broken query params (extra ? in url)
            const auto url = "https://strm.yandex.ru/music/music-strm-jsons/170252.bd1c6e46/kaltura/38426195.5.320/1630064015/1630046692/f52a7ae429f725afeb4bace544647cf2afcd7077a68b6f4b04c7a1e8e5b4d43a/master.m3u8?abc_id=94&from=ya-music?vsid=MTFaq59GKdD4E4s2FKdUapHHZZffjwa4pc19Uw5MmZMXxSTMx0000x1630063895";
            const auto result = AudioClientUtils::setVSIDToUrlParams(url, vsid);
            const auto expect = "https://strm.yandex.ru/music/music-strm-jsons/170252.bd1c6e46/kaltura/38426195.5.320/1630064015/1630046692/f52a7ae429f725afeb4bace544647cf2afcd7077a68b6f4b04c7a1e8e5b4d43a/master.m3u8?abc_id=94&from=ya-music%3Fvsid%3DMTFaq59GKdD4E4s2FKdUapHHZZffjwa4pc19Uw5MmZMXxSTMx0000x1630063895&vsid=ialak21suzo4kwfdydf4r92pahfer9waki040hcuxhehxSTMx0000x1630063895";
            UNIT_ASSERT_VALUES_EQUAL(result, expect);
        }

        {
            // test with mds url with query params
            const auto url = "https://s102iva.storage.yandex.net/get-mp3/273d767d52ca2918496a8a05bc13761c/0005ca8bfa06a34d/rmusic/U2FsdGVkX1-opLPLl-N77yY7AQy1FKo5-H4qCSQI4rlxG4l5EKhwvj81Z80ai--J-Vd2QqSBm85yIZUsYNVSu6xmnMpkXo008MC0luz-gHk/77795897763c0abf035bab559d2e3b8a1f330e202cccea86b7067b56237ff8b3?track-id=17829881&from=hollywood&play=false&uid=98688110";
            const auto result = AudioClientUtils::setVSIDToUrlParams(url, vsid);
            // note: params are re-ordered by alphabet
            const auto expect = "https://s102iva.storage.yandex.net/get-mp3/273d767d52ca2918496a8a05bc13761c/0005ca8bfa06a34d/rmusic/U2FsdGVkX1-opLPLl-N77yY7AQy1FKo5-H4qCSQI4rlxG4l5EKhwvj81Z80ai--J-Vd2QqSBm85yIZUsYNVSu6xmnMpkXo008MC0luz-gHk/77795897763c0abf035bab559d2e3b8a1f330e202cccea86b7067b56237ff8b3?from=hollywood&play=false&track-id=17829881&uid=98688110&vsid=ialak21suzo4kwfdydf4r92pahfer9waki040hcuxhehxSTMx0000x1630063895";
            UNIT_ASSERT_VALUES_EQUAL(result, expect);
        }

        {
            // test with mds url without query params
            const auto url = "https://s102iva.storage.yandex.net/get-mp3/273d767d52ca2918496a8a05bc13761c/0005ca8bfa06a34d/rmusic/U2FsdGVkX1-opLPLl-N77yY7AQy1FKo5-H4qCSQI4rlxG4l5EKhwvj81Z80ai--J-Vd2QqSBm85yIZUsYNVSu6xmnMpkXo008MC0luz-gHk/77795897763c0abf035bab559d2e3b8a1f330e202cccea86b7067b56237ff8b3";
            const auto result = AudioClientUtils::setVSIDToUrlParams(url, vsid);
            const auto expect = "https://s102iva.storage.yandex.net/get-mp3/273d767d52ca2918496a8a05bc13761c/0005ca8bfa06a34d/rmusic/U2FsdGVkX1-opLPLl-N77yY7AQy1FKo5-H4qCSQI4rlxG4l5EKhwvj81Z80ai--J-Vd2QqSBm85yIZUsYNVSu6xmnMpkXo008MC0luz-gHk/77795897763c0abf035bab559d2e3b8a1f330e202cccea86b7067b56237ff8b3?vsid=ialak21suzo4kwfdydf4r92pahfer9waki040hcuxhehxSTMx0000x1630063895";
            UNIT_ASSERT_VALUES_EQUAL(result, expect);
        }

        // test fragment part

        {
            // test url without query params and with frament
            const auto url = "https://strm.yandex.ru/music/music-strm-jsons/170252.bd1c6e46/kaltura/38426195.5.320/1630064015/1630046692/f52a7ae429f725afeb4bace544647cf2afcd7077a68b6f4b04c7a1e8e5b4d43a/master.m3u8#some-frag";
            const auto result = AudioClientUtils::setVSIDToUrlParams(url, vsid);
            const auto expect = "https://strm.yandex.ru/music/music-strm-jsons/170252.bd1c6e46/kaltura/38426195.5.320/1630064015/1630046692/f52a7ae429f725afeb4bace544647cf2afcd7077a68b6f4b04c7a1e8e5b4d43a/master.m3u8?vsid=ialak21suzo4kwfdydf4r92pahfer9waki040hcuxhehxSTMx0000x1630063895#some-frag";
            UNIT_ASSERT_VALUES_EQUAL(result, expect);
        }

        {
            // test url with query params and with fragment
            const auto url = "https://strm.yandex.ru/music/music-strm-jsons/170252.bd1c6e46/kaltura/38426195.5.320/1630064015/1630046692/f52a7ae429f725afeb4bace544647cf2afcd7077a68b6f4b04c7a1e8e5b4d43a/master.m3u8?abc_id=94&from=ya-music#some-frag2";
            const auto result = AudioClientUtils::setVSIDToUrlParams(url, vsid);
            const auto expect = "https://strm.yandex.ru/music/music-strm-jsons/170252.bd1c6e46/kaltura/38426195.5.320/1630064015/1630046692/f52a7ae429f725afeb4bace544647cf2afcd7077a68b6f4b04c7a1e8e5b4d43a/master.m3u8?abc_id=94&from=ya-music&vsid=ialak21suzo4kwfdydf4r92pahfer9waki040hcuxhehxSTMx0000x1630063895#some-frag2";
            UNIT_ASSERT_VALUES_EQUAL(result, expect);
        }
    }

} // suite
