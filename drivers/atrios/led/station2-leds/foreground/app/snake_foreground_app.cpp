#include "snake_foreground_app.h"

namespace {

    constexpr int INITIAL_SNAKE_LENGTH = 4;
    constexpr float SPEED_SCALE_FACTOR = 1.25f;

    constexpr float SNAKE_MAX_BRIGHTNESS = 0.75f;
    constexpr float SNAKE_MIN_BRIGHTNESS = 0.25f;
    constexpr float SNAKE_BLINK_LENGTH = 5.0f;
    constexpr SnakeForegroundApp::Duration SNAKE_BLINK_DURATION = std::chrono::milliseconds(600);
    constexpr float SNAKE_FOOD_BRIGHTNESS = 0.9f;
    constexpr float FOOD_MAX_BRIGHTNESS = 1.0f;
    constexpr float FOOD_MIN_BRIGHTNESS = 0.5f;
    constexpr SnakeForegroundApp::Duration FOOD_BLINK_DURATION = std::chrono::milliseconds(1000);
    constexpr float FOOD_BLINK_CIRCLE_MAX_BRIGHTNESS = 0.125f;
    constexpr float FOOD_BLINK_CIRCLE_MAX_RADIUS = 5.0f;
    constexpr float FOOD_BLINK_CIRCLES_COUNT = 1.5f;
    constexpr float FOOD_BLINK_CIRCLES_DISTANCE = FOOD_BLINK_CIRCLE_MAX_RADIUS / FOOD_BLINK_CIRCLES_COUNT;

} // namespace

std::string SnakeForegroundApp::getName() const {
    return "SnakeForegroundApp";
}

void SnakeForegroundApp::drawCurrentFrame(quasar::Screen& screen) {
    std::scoped_lock guard(mutex_);
    width_ = screen.width;
    height_ = screen.height;
    screenWasInitialized_ = true;
    if (!gameWasInitialized_) {
        return;
    }
    draw(screen);
}

void SnakeForegroundApp::tick() {
    std::scoped_lock guard(mutex_);
    if (!screenWasInitialized_) {
        return;
    }
    if (!gameWasInitialized_) {
        resetGame();
        gameWasInitialized_ = true;
    }
    if (isPaused_) {
        return;
    }
    SnakeElement newHead(snake_.front());
    switch (currentDirection_) {
        case Direction::LEFT:
            newHead.position.x = (newHead.position.x + width_ - 1) % width_;
            break;
        case Direction::TOP:
            newHead.position.y = (newHead.position.y + height_ - 1) % height_;
            break;
        case Direction::RIGHT:
            newHead.position.x = (newHead.position.x + 1) % width_;
            break;
        case Direction::BOTTOM:
            newHead.position.y = (newHead.position.y + 1) % height_;
            break;
    }
    if (isPointUnderSnake(newHead.position)) {
        resetGame();
        return;
    }
    if (newHead.position == foodPosition_) {
        newHead.containsFood = true;
        moveFoodToRandomPlace();
        playPop();
    } else {
        newHead.containsFood = false;
    }
    snake_.push_front(newHead);
    SnakeElement& tail = snake_.back();
    if (tail.containsFood) {
        tail.containsFood = false;
    } else {
        snake_.pop_back();
    }
}

void SnakeForegroundApp::draw(quasar::Screen& screen) const {
    std::fill(screen.data.begin(), screen.data.end(), BGR24::black);
    drawFood(screen);
    drawSnake(screen);
}

void SnakeForegroundApp::drawFood(quasar::Screen& screen) const {
    const auto animaionPercentage = calcAnimationPercentage(FOOD_BLINK_DURATION);
    for (int dx = -FOOD_BLINK_CIRCLE_MAX_RADIUS; dx <= FOOD_BLINK_CIRCLE_MAX_RADIUS; dx++) {
        for (int dy = -FOOD_BLINK_CIRCLE_MAX_RADIUS; dy <= FOOD_BLINK_CIRCLE_MAX_RADIUS; dy++) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            int x = foodPosition_.x + dx;
            if (x < 0 || x >= width_) {
                continue;
            }
            int y = foodPosition_.y + dy;
            if (y < 0 || y >= height_) {
                continue;
            }
            float radius = sqrt((float)(dx * dx + dy * dy));
            if (radius > FOOD_BLINK_CIRCLE_MAX_RADIUS) {
                continue;
            }
            float percentage = radius / FOOD_BLINK_CIRCLES_DISTANCE;
            percentage -= floor(percentage);
            percentage -= animaionPercentage;
            percentage = (percentage < 0) ? percentage + 1 : percentage;
            percentage = std::cos(percentage * M_PI * 2);
            percentage = (percentage + 1) / 2;
            float fadeFactor = 1 - radius / FOOD_BLINK_CIRCLE_MAX_RADIUS;
            percentage *= fadeFactor;
            auto brightness = FOOD_BLINK_CIRCLE_MAX_BRIGHTNESS * percentage;
            screen.elem(x, y) = brightness;
        }
    }

    const auto foodBrightness = FOOD_MIN_BRIGHTNESS + (FOOD_MAX_BRIGHTNESS - FOOD_MIN_BRIGHTNESS) * animaionPercentage;
    screen.elem(foodPosition_.x, foodPosition_.y) = foodBrightness;
}

void SnakeForegroundApp::playPop() {
    sdk_->getFilePlayerCapability()->playSoundFile("pop.wav", std::nullopt);
}

void SnakeForegroundApp::drawSnake(quasar::Screen& screen) const {
    int i = 0;
    auto animaionOffset = calcAnimationPercentage(SNAKE_BLINK_DURATION);
    std::for_each(snake_.begin(), snake_.end(), [&](const SnakeElement& element) {
        float brightness;
        if (element.containsFood) {
            brightness = SNAKE_FOOD_BRIGHTNESS;
        } else {
            float blinkPercentage = (float)i / SNAKE_BLINK_LENGTH;
            blinkPercentage -= floor(blinkPercentage);
            blinkPercentage += animaionOffset;
            blinkPercentage = (blinkPercentage > 1) ? blinkPercentage - 1 : blinkPercentage;
            float brightnessPercentage = (std::sin(blinkPercentage * 2 * M_PI) + 1) / 2;
            brightness = SNAKE_MIN_BRIGHTNESS + (SNAKE_MAX_BRIGHTNESS - SNAKE_MIN_BRIGHTNESS) * brightnessPercentage;
        }
        screen.elem(element.position.x, element.position.y) = brightness;
        i++;
    });
}

float SnakeForegroundApp::calcAnimationPercentage(const SnakeForegroundApp::Duration& animationDuration) {
    const auto now = std::chrono::steady_clock::now();
    const auto toNow = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch();
    const auto toNowMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(toNow).count();
    const auto durationMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(animationDuration).count();
    const auto animationOffetMilliseconds = toNowMilliseconds % durationMilliseconds;
    return (float)animationOffetMilliseconds / (float)(durationMilliseconds - 1);
}

bool SnakeForegroundApp::handleRcu(const RcuButtonEventMessage& rcuMessage) {
    if (rcuMessage.getAction() != quasar::proto::ButtonEvent::UP) {
        return false;
    }
    switch (rcuMessage.getButtonKey()) {
        case rcu_button::Key::NAV_UP:
            tryUpdateDirection(Direction::TOP);
            break;
        case rcu_button::Key::NAV_DOWN:
            tryUpdateDirection(Direction::BOTTOM);
            break;
        case rcu_button::Key::NAV_LEFT:
            tryUpdateDirection(Direction::LEFT);
            break;
        case rcu_button::Key::NAV_RIGHT:
            tryUpdateDirection(Direction::RIGHT);
            break;
        case rcu_button::Key::VOLUME_UP:
            scalePauseInterTicks(1 / SPEED_SCALE_FACTOR);
            break;
        case rcu_button::Key::VOLUME_DOWN:
            scalePauseInterTicks(SPEED_SCALE_FACTOR);
            break;
        case rcu_button::Key::MAIN:
        case rcu_button::Key::VOLUME_MUTE:
            isPaused_ = !isPaused_;
            break;
        default:
            return false;
    }
    return true;
}

void SnakeForegroundApp::scalePauseInterTicks(float factor) {
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(nextTicExecutor_.periodTime()).count();
    milliseconds *= factor;
    if (milliseconds < 1) {
        milliseconds = 1;
    }
    nextTicExecutor_.setPeriodTime(std::chrono::milliseconds(milliseconds));
}

void SnakeForegroundApp::tryUpdateDirection(SnakeForegroundApp::Direction newDirection) {
    std::scoped_lock guard(mutex_);
    if (getDirectionOrientation(newDirection) == getDirectionOrientation(currentDirection_)) {
        return;
    }
    currentDirection_ = newDirection;
}

SnakeForegroundApp::Orientation SnakeForegroundApp::getDirectionOrientation(SnakeForegroundApp::Direction direction) {
    switch (direction) {
        case Direction::LEFT:
        case Direction::RIGHT:
            return Orientation::HORIZONTAL;
        case Direction::TOP:
        case Direction::BOTTOM:
            return Orientation::VERTICAL;
    }
}

void SnakeForegroundApp::resetGame() {
    resetSnake();
    moveFoodToRandomPlace();
}

void SnakeForegroundApp::resetSnake() {
    currentDirection_ = Direction::RIGHT;
    snake_.clear();
    int headX = width_ / 2;
    int headY = height_ / 2;
    for (int x = headX - INITIAL_SNAKE_LENGTH + 1; x <= headX; x++) {
        snake_.push_front(SnakeElement{{x, headY}});
    }
}

void SnakeForegroundApp::moveFoodToRandomPlace() {
    std::uniform_int_distribution<> xDistribution(0, width_ - 1);
    std::uniform_int_distribution<> yDistribution(0, height_ - 1);
    do {
        foodPosition_.x = xDistribution(randomGenerator_);
        foodPosition_.y = yDistribution(randomGenerator_);
    } while (isPointUnderSnake(foodPosition_));
}

bool SnakeForegroundApp::isPointUnderSnake(quasar::Point& position) const {
    return std::any_of(
        snake_.begin(), snake_.end(),
        [&position](auto& snake_Item) {
            return snake_Item.position == position;
        });
}

SnakeForegroundApp::SnakeForegroundApp(std::shared_ptr<YandexIO::SDKInterface> sdk)
    : sdk_(std::move(sdk))
    , randomGenerator_(std::random_device()())
    , nextTicExecutor_(
          std::bind(&SnakeForegroundApp::tick, this),
          DEFAULT_PAUSE_INTER_TICKS,
          quasar::PeriodicExecutor::PeriodicType::SLEEP_FIRST)
{
}
