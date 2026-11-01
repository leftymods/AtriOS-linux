#pragma once

#include "base/foreground_app.h"

#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/draw_utils.h>

#include <yandex_io/protos/model_objects.pb.h>
#include <yandex_io/libs/threading/periodic_executor.h>
#include <yandex_io/sdk/sdk_interface.h>

#include <deque>
#include <random>
#include <memory>

class SnakeForegroundApp: public ForegroundApp {
public:
    explicit SnakeForegroundApp(std::shared_ptr<YandexIO::SDKInterface> sdk);

    [[nodiscard]] std::string getName() const override;

    void drawCurrentFrame(quasar::Screen& screen) override;

    [[nodiscard]] bool handleRcu(const RcuButtonEventMessage& rcuMessage) override;

    using Duration = std::chrono::milliseconds;

private:
    struct SnakeElement {
        quasar::Point position{};
        bool containsFood = false;
    };

    enum class Orientation {
        HORIZONTAL,
        VERTICAL
    };

    enum class Direction {
        LEFT,
        TOP,
        RIGHT,
        BOTTOM
    };

    static Orientation getDirectionOrientation(Direction direction);

    void resetGame();

    void resetSnake();

    void moveFoodToRandomPlace();

    [[nodiscard]] bool isPointUnderSnake(quasar::Point& position) const;

    void draw(quasar::Screen& screen) const;

    void drawFood(quasar::Screen& screen) const;

    void drawSnake(quasar::Screen& screen) const;

    void tick();

    void tryUpdateDirection(Direction newDirection);

    void scalePauseInterTicks(float factor);

    static float calcAnimationPercentage(const Duration& animationDuration);

    void playPop();

private:
    static constexpr Duration DEFAULT_PAUSE_INTER_TICKS = std::chrono::milliseconds(300);

    const std::shared_ptr<YandexIO::SDKInterface> sdk_;
    bool gameWasInitialized_ = false;
    bool screenWasInitialized_ = false;

    int width_ = 0;
    int height_ = 0;

    bool isPaused_ = false;

    Direction currentDirection_;
    std::deque<SnakeElement> snake_;

    quasar::Point foodPosition_{};

    std::mt19937 randomGenerator_;

    std::mutex mutex_;

    quasar::PeriodicExecutor nextTicExecutor_;
};
