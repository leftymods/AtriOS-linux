#include "mp_listener.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/self_destroyer/self_destroyer_utils.h>
#include <yandex_io/libs/telemetry/telemetry.h>

#include <chrono>
#include <limits>

YIO_DEFINE_LOG_MODULE("media");

using namespace quasar;

MPListener::MPListener(std::shared_ptr<YandexIO::IDevice> device, YandexMusicPlayer* yandexMusicPlayer)
    : device_(std::move(device))
    , yandexMusicPlayer_(yandexMusicPlayer)
    , selfDestroyer_(
          SelfDestroyerUtils::create(
              "media",
              device_,
              "mediad"))
{
}

void MPListener::onError(const std::string& message) {
    Json::Value errorJson;
    errorJson["message"] = message;
    auto track = yandexMusicPlayer_->getCurrentTrack();
    std::string id;
    std::string url;
    if (track) {
        id = track->id;
        url = track->url;
        errorJson["url"] = track->url;
    }
    YIO_LOG_ERROR_EVENT("MPListener.Error", "MPListener::onError " << message << ". url: " << url);
    yandexMusicPlayer_->reportEventWithTrackId("AudioError", errorJson, id);
    yandexMusicPlayer_->handleError();
}

void MPListener::onStart() {
    YIO_LOG_INFO("MPListener::onStart");
    yandexMusicPlayer_->reportLatencyNext();
    device_->telemetry()->reportLatency(std::move(endToStartLatencyPoint_), "musicPlayerStartedFromPrev");

    auto track = yandexMusicPlayer_->getCurrentTrack();
    std::string id;
    if (track) {
        id = track->id;
    }
    yandexMusicPlayer_->reportEventWithTrackId("playAudio", id, false, true);
    const auto lastPlayCommandTimePoint = yandexMusicPlayer_->getAndResetLastPlayCommandTimePoint();
    if (lastPlayCommandTimePoint.has_value()) {
        Json::Value eventJson;
        const auto now = std::chrono::steady_clock::now();
        const auto timeDiff = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPlayCommandTimePoint.value());
        eventJson["passedTime"] = int64_t(timeDiff.count());
        yandexMusicPlayer_->reportEventWithTrackId("musicPlayerPlayRequestFulfill", eventJson, id);
    }
    yandexMusicPlayer_->handleStart();
}

void MPListener::onEnd() {
    YIO_LOG_INFO("MPListener::onEnd");

    auto track = yandexMusicPlayer_->getCurrentTrack();
    std::string id;
    if (track) {
        id = track->id;
    }
    yandexMusicPlayer_->reportEventWithTrackId("endAudio", id);
    selfDestroyer_->destroySelfIfNeed();

    // somehow onEnd is called when device is getting back online,
    // so request next song only if in playing state
    if (yandexMusicPlayer_->isPlaying()) {
        Json::Value nextOptions;
        nextOptions["skip"] = false;
        nextOptions["ignoreIfNotPlaying"] = true;
        endToStartLatencyPoint_ = device_->telemetry()->createLatencyPoint();

        try {
            yandexMusicPlayer_->processCommand(commands::NEXT, nextOptions);
        } catch (const std::exception& e) {
            YIO_LOG_ERROR_EVENT("MPListener.FailedProcessCommand", "Exception in processCommand: " << e.what());
            yandexMusicPlayer_->handleError();
        }
    }
}

void MPListener::onSeeked() {
}

void MPListener::onPaused() {
    yandexMusicPlayer_->handleStop();

    auto track = yandexMusicPlayer_->getCurrentTrack();
    std::string id = track ? track->id : "";
    yandexMusicPlayer_->reportLatencyPause();
    yandexMusicPlayer_->reportEventWithTrackId("pauseAudio", id, false, true);
}

void MPListener::onResumed() {
    auto track = yandexMusicPlayer_->getCurrentTrack();
    std::string id = track ? track->id : "";
    yandexMusicPlayer_->reportLatencyResume();
    yandexMusicPlayer_->reportEventWithTrackId("resumeAudio", id, false, true);
}

void MPListener::onProgress(int position, int duration) {
    yandexMusicPlayer_->setCurrentProgress(position, duration);
    YIO_LOG_TRACE(std::to_string(position) + "/" + std::to_string(duration));
}

void MPListener::onBufferingStart() {
    YIO_LOG_INFO("MPListener::onBufferingStart");
    bufferingLatencyPoint_ = device_->telemetry()->createLatencyPoint();
    device_->telemetry()->reportLatency(bufferingLatencyPoint_, "musicPlayerBufferingStart");
}

void MPListener::onBufferingEnd() {
    YIO_LOG_INFO("MPListener::onBufferingEnd");
    device_->telemetry()->reportLatency(std::move(bufferingLatencyPoint_), "musicPlayerBufferingEnd");
}

void MPListener::onStopped() {
    yandexMusicPlayer_->handleStop();
}

void MPListener::onBufferStalled() {
    YIO_LOG_INFO("MPListener::onBufferStalled");
    auto track = yandexMusicPlayer_->getCurrentTrack();
    Json::Value payload = Json::objectValue;
    std::string id;
    if (track) {
        id = track->id;
        payload["url"] = track->url;
    }
    yandexMusicPlayer_->reportEventWithTrackId("musicPlayerBufferStalled", payload, id);
}
