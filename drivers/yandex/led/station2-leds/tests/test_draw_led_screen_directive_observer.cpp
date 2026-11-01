#include <smart_devices/platforms/yandexstation_2/platformd/leds/draw_led_screen_directive_observer.h>

#include <library/cpp/testing/unittest/registar.h>

#include <yandex_io/libs/base/directives.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/modules/leds/led_controller/null_led_controller.h>
#include <yandex_io/modules/leds/led_manager/ng/default_led_devices.h>
#include <yandex_io/tests/testlib/test_http_server.h>
#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>
#include <yandex_io/tests/testlib/null_sdk/null_sdk_interface.h>

#include <functional>
#include <stdexcept>
#include <vector>

using namespace quasar;
using namespace quasar::TestUtils;

unsigned char singlePixelGif[] = {
    0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x21, 0xf9, 0x04, 0x01, 0x0a, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3b};

std::string convertToString(char* a, int size) {
    std::stringstream sstream;
    for (int i = 0; i < size; i++) {
        sstream << a[i];
    }
    return sstream.str();
}

class Fixture: public QuasarUnitTestFixture {
public:
    using Base = QuasarUnitTestFixture;

    void SetUp(NUnitTest::TTestContext& context) override {
        Base::SetUp(context);

        auto& config = getDeviceForTests()->configuration()->getMutableConfig(testGuard_);
        config["maind"]["aliceFrontalAnimationGifsCache"] = "./temp1/";

        mockBackend_.onHandlePayload = std::bind(&Fixture::handleRequest,
                                                 std::placeholders::_1, std::placeholders::_2,
                                                 std::placeholders::_3);

        int backendPort = mockBackend_.start(getPort());
        mockBackendUrl_ = "http://localhost:" + std::to_string(backendPort);
    }

    void TearDown(NUnitTest::TTestContext& context) override {
        Base::TearDown(context);
    }

protected:
    YandexIO::Configuration::TestGuard testGuard_;

    static void handleRequest(const TestHttpServer::Headers& /* headers */, const std::string& /* paylaod */,
                              TestHttpServer::HttpConnection& handler) {
        handler.doReplay(200, "image/gif", convertToString((char*)singlePixelGif, 43), {});
    }

    std::string mockBackendUrl_;
    TestHttpServer mockBackend_;
};

class TestLedManager: public quasar::LedManager {
public:
    TestLedManager()
        : LedManager(std::make_unique<DefaultLedDevices>(std::make_unique<quasar::NullLedController>()), ".")
    {
    }

    void play(std::shared_ptr<AnimationConductor> conductor) override {
        lastAnimation_ = conductor->getCurrentComposition()->getAnimations().front()->getName();
    }

    std::shared_ptr<LedPattern> getPattern(const std::string& name) const override {
        return std::make_shared<LedPattern>(1, name, ledDevices_->getDefaultDevice());
    }

    mutable std::shared_ptr<quasar::ListeningPattern> listeningPattern_;
    std::string lastAnimation_;
};

class YandexStationFrontalControllerMock: public YandexStationFrontalController {
public:
    YandexStationFrontalControllerMock()
        : YandexStationFrontalController("-", std::make_shared<ClockDisplayStateHolder>("/state_file.dat", std::make_shared<YandexIO::NullSDKInterface>()))
    {
    }

    void drawBuffer(const Frame& frame) override {
        YandexStationFrontalController::drawBuffer(frame);
    }

    void clearFrame() override {
        YandexStationFrontalController::clearFrame();
    }
};

class LedDevicesImplTest: public LedDevicesImpl {
public:
    LedDevicesImplTest()
        : LedDevicesImpl("deviceId", "", std::make_shared<ClockDisplayStateHolder>("/state_file.dat", std::make_shared<YandexIO::NullSDKInterface>()))
    {
    }

    std::shared_ptr<YandexStationFrontalController> getLedScreen() override {
        return yandexStationFrontalController;
    }

    virtual ~LedDevicesImplTest() {
    }

private:
    std::shared_ptr<YandexStationFrontalController> yandexStationFrontalController = std::make_shared<YandexStationFrontalControllerMock>();
};

namespace quasar {
    Y_UNIT_TEST_SUITE_F(testDrawLedScreenDirectiveObserver, Fixture) {
        Y_UNIT_TEST(testTwoImages) {
            auto ledController = std::make_shared<NullLedController>();
            std::shared_ptr<quasar::LedManager> testLedManager = std::make_shared<TestLedManager>();
            std::shared_ptr<LedDevicesImplTest> ledDevices = std::make_shared<LedDevicesImplTest>();
            DrawLedScreenDirectiveObserver drawLedScreenDirectiveObserver(testLedManager, ledDevices, getDeviceForTests());

            drawLedScreenDirectiveObserver.onDrawLedScreenDirective(quasar::Directives::DRAW_LED_SCREEN,
                                                                    "{\n"
                                                                    "       \"animation_sequence\" : \n"
                                                                    "       [ \n"
                                                                    "         {\n"
                                                                    "             \"frontal_led_image\": \"" +
                                                                        mockBackendUrl_ + "/led_screen/cloud-3.gif\"\n"
                                                                                          "         },\n"
                                                                                          "         {\n"
                                                                                          "             \"frontal_led_image\": \"" +
                                                                        mockBackendUrl_ + "/led_screen/cloud-4.gif\",\n"
                                                                                          "             \"endless\": true\n"
                                                                                          "         }\n"
                                                                                          "         \n"
                                                                                          "       ]\n"
                                                                                          "   }");

            drawLedScreenDirectiveObserver.stop();
            UNIT_ASSERT(!drawLedScreenDirectiveObserver.isPlaying());
            UNIT_ASSERT(!drawLedScreenDirectiveObserver.getActiveConductor());
        }

        Y_UNIT_TEST(testZeroImagesEgError) {
            auto ledController = std::make_shared<NullLedController>();
            std::shared_ptr<quasar::LedManager> testLedManager = std::make_shared<TestLedManager>();
            std::shared_ptr<LedDevicesImplTest> ledDevices = std::make_shared<LedDevicesImplTest>();
            DrawLedScreenDirectiveObserver drawLedScreenDirectiveObserver(testLedManager, ledDevices, getDeviceForTests());

            bool isCalled = false;
            drawLedScreenDirectiveObserver.setEventListener([&]() { isCalled = true; });
            drawLedScreenDirectiveObserver.onDrawLedScreenDirective(quasar::Directives::DRAW_LED_SCREEN,
                                                                    "{\"payload\": {\n"
                                                                    "       \"animation_sequence\" : \n"
                                                                    "       []\n"
                                                                    "   }}");

            UNIT_ASSERT(!isCalled);
            UNIT_ASSERT(!drawLedScreenDirectiveObserver.isPlaying());
            UNIT_ASSERT(!drawLedScreenDirectiveObserver.getActiveConductor());
        }

    } // Y_UNIT_TEST_SUITE(testDrawLedScreenDirectiveObserver)

} // namespace quasar
