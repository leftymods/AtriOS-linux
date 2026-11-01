#include <smart_devices/platforms/yandexstation_2/platformd/leds/gif_animation.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/timer/timer_frontal_animation.h>

#include <contrib/libs/cxxsupp/libcxx/include/fstream>

#include <library/cpp/testing/unittest/registar.h>

#include <yandex_io/libs/logging/logging.h>

#include <functional>
#include <stdexcept>
#include <vector>

namespace quasar {

    Y_UNIT_TEST_SUITE(testTimerFrontalAnimation) {
        Y_UNIT_TEST(testTimerFrame) {
            {
                TimerFrame timerFrame;
                auto replace = timerFrame.getReplace();
                UNIT_ASSERT(replace.empty());
            }
            {
                TimerFrame timerFrame{TimerFrameType::COUNTDOWN,
                                      TimerFrameHelper::toClockSymbols(std::chrono::milliseconds(999)),
                                      TimerFrameHelper::toClockSymbols(std::chrono::milliseconds(-1))};
                auto replace = timerFrame.getReplace();
                for (const auto& item : replace) {
                    UNIT_ASSERT(item.symbol == "0_0");
                }
            }
        }
        Y_UNIT_TEST(testTimerFrame59) {
            {
                auto start = std::chrono::minutes(59);
                TimerFrame timerFrame{TimerFrameType::COUNTDOWN,
                                      TimerFrameHelper::toClockSymbols(start),
                                      TimerFrameHelper::toClockSymbols(start - std::chrono::seconds(1))};
                auto replace = timerFrame.getReplace();
                UNIT_ASSERT(replace[0].symbol == "5_5");
                UNIT_ASSERT(replace[1].symbol == "9_8");
                UNIT_ASSERT(replace[2].symbol == "0_5");
                UNIT_ASSERT(replace[3].symbol == "0_9");
            }
        }

        Y_UNIT_TEST(testTimerFrameHelper) {
            std::tm tm = {};
            std::stringstream ss("Jul 01 2021 06:35:00");
            ss >> std::get_time(&tm, "%b %d %Y %H:%M:%S");
            auto baseTime = std::chrono::system_clock::from_time_t(std::mktime(&tm));

            TimerFrameHelper timerFrameHelper(baseTime + std::chrono::seconds(45));
            // animationShift 300 ms
            auto image = timerFrameHelper.getFrame(baseTime + std::chrono::seconds(40) + std::chrono::milliseconds(300));
            UNIT_ASSERT_EQUAL(image.type, TimerFrameType::COUNTDOWN);

            image = timerFrameHelper.getFrame(baseTime + std::chrono::seconds(40) + std::chrono::milliseconds(20) + std::chrono::milliseconds(300));
            UNIT_ASSERT_EQUAL(image.type, TimerFrameType::COUNTDOWN);

            //"Thu Feb 21 06:35:40.300000 2021" - 1300ms;
            image = timerFrameHelper.getFrame(baseTime + std::chrono::seconds(40) + std::chrono::milliseconds(300) + std::chrono::milliseconds(300));
            UNIT_ASSERT_EQUAL(image.type, TimerFrameType::COUNTDOWN);
            //"Thu Feb 21 05:35:44.999999 2021";
            image = timerFrameHelper.getFrame(baseTime - std::chrono::hours(1) + std::chrono::seconds(45) - std::chrono::milliseconds(1));
            UNIT_ASSERT_EQUAL(image.type, TimerFrameType::EARLY);
        }

    } // Y_UNIT_TEST_SUITE(testTimerFrontalAnimation)

} // namespace quasar
