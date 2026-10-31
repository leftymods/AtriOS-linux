#include <smart_devices/platforms/yandexstation_2/platformd/leds/frontal_animation/cache_manager.h>

#include <contrib/libs/cxxsupp/libcxx/include/fstream>

#include <library/cpp/testing/unittest/registar.h>

#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/telemetry/null/null_metrica.h>

#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <fstream>
#include <functional>
#include <stdexcept>
#include <vector>

namespace quasar {
    static const std::string FIRST = "https://static-alice.s3.yandex.net/led/1.gif";
    static const std::string SECOND = "https://static-alice.s3.yandex.net/led/2.gif";
    static const std::string THIRD = "https://static-alice.s3.yandex.net/led/3.gif";
    static const std::string CACHE_DIR = "./temp1/";

    class Fixture: public QuasarUnitTestFixture {
    public:
        Fixture()
            : telemetry_(std::make_shared<NullMetrica>())
        {
        }

    protected:
        const std::shared_ptr<YandexIO::ITelemetry> telemetry_;
    };

    Y_UNIT_TEST_SUITE_F(testGifCache, Fixture) {
        Y_UNIT_TEST(testOneCached) {
            CacheManager cacheManager("__QUASAR__testOneCached", 1, 1000, 0, CACHE_DIR, telemetry_);
            auto result = cacheManager.getFilename(FIRST);
            UNIT_ASSERT(!result.hit);
            std::ofstream file(result.filename);
            UNIT_ASSERT(file.good());
            cacheManager.updateCache(FIRST);
            result = cacheManager.getFilename(FIRST);
            UNIT_ASSERT(result.hit);
        }

        Y_UNIT_TEST(testTwoCached) {
            CacheManager cacheManager("__QUASAR__testTwoCached", 2, 1000, 0, CACHE_DIR, telemetry_);
            auto result = cacheManager.getFilename(FIRST);
            cacheManager.updateCache(FIRST);
            result = cacheManager.getFilename(SECOND);
            UNIT_ASSERT(!result.hit);
            cacheManager.updateCache(SECOND);
            result = cacheManager.getFilename(THIRD);
            cacheManager.updateCache(THIRD);
            result = cacheManager.getFilename(FIRST);
            UNIT_ASSERT(!result.hit);
        }

        Y_UNIT_TEST(testClearSize) {
            std::string folder{"__QUASAR__testClearSize"};
            CacheManager cacheManager(folder, 3, 90, 120, CACHE_DIR, telemetry_);
            const auto first = cacheManager.updateCache(FIRST);
            {
                const std::string filepath = CACHE_DIR + folder + "/" + first;
                std::ofstream stream{filepath};
                std::string buffer(100000, ' ');
                stream << buffer;
            }
            int deleted = cacheManager.clear();
            UNIT_ASSERT_EQUAL(deleted, 1);
        }

        Y_UNIT_TEST(testTransformUrl) {
            CacheManager cacheManager("__QUASAR__testTransformUrl", 2, 1000, 0, CACHE_DIR, telemetry_);
            UNIT_ASSERT_EQUAL(cacheManager.transformUrl(FIRST), "https:,,static-alice.s3.yandex.net,led,1.gif");
        }

    } // Y_UNIT_TEST_SUITE(testGifCache)

} // namespace quasar
