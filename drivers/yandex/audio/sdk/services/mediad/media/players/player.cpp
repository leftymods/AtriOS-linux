#include "player.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ete_metrics/ete_util.h>
#include <yandex_io/libs/json_utils/json_utils.h>

#include <chrono>
#include <utility>

using namespace quasar;

Player::Player(std::shared_ptr<YandexIO::IDevice> device, Player::OnStateChange onStateChange,
               Player::OnPlayStartChange onStart, Player::OnError onError)
    : device_(std::move(device))
    , onStateChange_(std::move(onStateChange))
    , onStart_(std::move(onStart))
    , onError_(std::move(onError))
{
}

Player::~Player() = default;

void Player::setEqualizerConfig(const YandexIO::EqualizerConfig& /*config*/) {
}

Player::ChangeConfigResult Player::updateConfig(const Json::Value& /* config */) {
    return ChangeConfigResult::NO_CHANGES;
}

PlayerType Player::type() const {
    return type_;
}

void Player::reportEvent(const std::string& event, bool skipDatabase) const {
    Json::Value eventJson;
    reportEvent(event, eventJson, skipDatabase);
}

void Player::reportEvent(const std::string& event, Json::Value eventJson, bool skipDatabase) const {
    reportEventWithTrackId(event, std::move(eventJson), getTrackIdInternal(), skipDatabase);
}

void Player::reportEventWithTrackId(const std::string& event, Json::Value eventJson, const std::string& trackId, bool skipDatabase, bool removeCurrentVinsId) const {
    const YandexIO::ITelemetry::Flags flags = skipDatabase ? YandexIO::ITelemetry::SKIP_DATABASE : 0;

    if (!vinsInitRequestId_.empty()) {
        eventJson["vinsRequestId"] = vinsInitRequestId_;
    }
    if (!currentVinsRequestId_.empty()) {
        appendETEHeader(currentVinsRequestId_, eventJson);
        if (removeCurrentVinsId) {
            currentVinsRequestId_.clear();
        }
    }
    eventJson["trackId"] = trackId;
    device_->telemetry()->reportEvent(event, jsonToString(eventJson), flags);
}

void Player::reportEventWithTrackId(const std::string& event, const std::string& trackId, bool skipDatabase, bool removeCurrentVinsId) const {
    Json::Value eventJson;
    reportEventWithTrackId(event, eventJson, trackId, skipDatabase, removeCurrentVinsId);
}

int64_t Player::getNowMs() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
}
