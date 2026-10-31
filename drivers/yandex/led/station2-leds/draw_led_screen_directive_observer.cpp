#include "draw_led_screen_directive_observer.h"
#include "gif_animation.h"
#include "repeated_animation.h"

#include <yandex_io/libs/base/directives.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/http_client/http_client.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_conductor.h>
#include <yandex_io/modules/leds/led_manager/ng/default_animation_composition.h>

#include <util/generic/scope.h>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

using namespace quasar;

DrawLedScreenDirectiveObserver::DrawLedScreenDirectiveObserver(std::shared_ptr<quasar::LedManager> ledManager,
                                                               std::shared_ptr<LedDevicesImpl> ledDevices, std::shared_ptr<YandexIO::IDevice> device)
    : device_(std::move(device))
    , ledManager_(std::move(ledManager))
    , ledDevices_(std::move(ledDevices))
    , cacheManager_(device_->softwareVersion(),
                    CacheManager::DEFAULT_SIZE_ENTRIES,
                    CacheManager::DEFAULT_SIZE_KBYTES,
                    CacheManager::DEFAULT_CLEAR_CACHE_PERIOD_MINUTES,
                    quasar::getString(device_->configuration()->getServiceConfig("maind"), "aliceFrontalAnimationGifsCache"),
                    device_->telemetry())
{
}

DrawLedScreenDirectiveObserver::~DrawLedScreenDirectiveObserver() {
    callbackQueue_.destroy();
}

void DrawLedScreenDirectiveObserver::onDrawLedScreenDirective(const std::string& name, const std::string& jsonPayload) {
    callbackQueue_.add([this, name, jsonPayload] { processDirective(name, jsonPayload); });
}

void DrawLedScreenDirectiveObserver::processDirective(const std::string& name, const std::string& jsonPayload) {
    if (name != quasar::Directives::DRAW_LED_SCREEN) {
        return;
    }

    auto conductor = extractConductor(jsonPayload);
    if (conductor) {
        play(conductor.value());
    }
}

std::optional<std::shared_ptr<DefaultAnimationConductor>> DrawLedScreenDirectiveObserver::extractConductor(const std::string& jsonPayload) {
    Json::Value payload;

    try {
        payload = quasar::parseJson(jsonPayload);
    } catch (const Json::Exception& e) {
        YIO_LOG_ERROR_EVENT("DrawLedScreenDirectiveObserver.InvalidDirectiveJson", "Cant't parse directive payload: " << jsonPayload);
        return std::nullopt;
    }

    Json::Value animationsJsons;
    if (const auto& a = payload["animation_sequence"]; a.isArray()) {
        animationsJsons = a;
    } else {
        YIO_LOG_ERROR_EVENT("DrawLedScreenDirectiveObserver.AbsentAnimationSequence", "Can't find animation_sequence in " << jsonPayload);
        return std::nullopt;
    }

    std::vector<const std::shared_ptr<AnimationComposition>> compositions;
    compositions.reserve(animationsJsons.size());

    bool endless = false;

    for (const auto& animationsJson : animationsJsons) {
        if (endless) {
            YIO_LOG_ERROR_EVENT("DrawLedScreenDirectiveObserver.MoreThanOneEndlessAnimation", "endless should be present no more than once, on the last element");
        }
        std::string url;

        try {
            url = quasar::getString(animationsJson, "frontal_led_image");
        } catch (const std::runtime_error& e) {
            YIO_LOG_ERROR_EVENT("DrawLedScreenDirectiveObserver.InvalidDirectiveFrontalLedImageUrl", "Unexpected animation: " << e.what());
            device_->telemetry()->reportEvent("frontalAnimationDirectiveFail", e.what());
            continue;
        }

        endless = quasar::tryGetBool(animationsJson, "endless", false);
    }
    std::set<quasar::HttpClient::DownloadFileTask> downloadFileTasks;
    std::map<std::string, std::string> urlsToFilenames;

    for (const auto& animationsJson : animationsJsons) {
        std::string url = quasar::getString(animationsJson, "frontal_led_image");
        const CacheManager::CacheResult& cacheResult = cacheManager_.getFilename(url);
        if (cacheResult.hit) {
            YIO_LOG_DEBUG("cached: " << url << " : " << cacheResult.filename);
            urlsToFilenames[url] = cacheResult.filename;
        } else {
            std::remove(cacheResult.filename.c_str());
            downloadFileTasks.insert(quasar::HttpClient::DownloadFileTask{.url = url, .outputFileName = cacheResult.filename});
        }
    }

    quasar::HttpClient httpClient("mds", device_);
    httpClient.setTimeout(std::chrono::milliseconds{3000});
    httpClient.setConnectionTimeout(std::chrono::milliseconds(3000));
    httpClient.setRetriesCount(3);

    std::vector<quasar::HttpClient::DownloadFileTask> downloadFileTasksList;
    downloadFileTasksList.reserve(downloadFileTasks.size());
    std::copy(downloadFileTasks.begin(), downloadFileTasks.end(), std::back_inserter(downloadFileTasksList));
    try {
        httpClient.download("frontal-led-image", downloadFileTasksList);
    } catch (const std::runtime_error& e) {
        YIO_LOG_ERROR_EVENT("DrawLedScreenDirectiveObserver.DownloadAnimationFailed", "Can't download animation, error: " << e.what());
        device_->telemetry()->reportEvent("frontalAnimationDownloadFail", e.what());
        state_ = State::STOPPED;
        return std::nullopt;
    }

    std::vector<std::string> toRemove;
    Y_DEFER {
        for (const auto& item : toRemove) {
            std::remove(item.c_str());
        }
    };

    for (auto& downloadFileTask : downloadFileTasksList) {
        if (hasNoCacheHeader(downloadFileTask.responseHeaders)) {
            YIO_LOG_DEBUG("not caching " << downloadFileTask.outputFileName);
            toRemove.emplace_back(downloadFileTask.outputFileName);
        } else {
            cacheManager_.updateCache(downloadFileTask.url);
        }
        urlsToFilenames[downloadFileTask.url] = downloadFileTask.outputFileName;
    }

    auto speakingPattern = ledManager_->getPattern("speaking.led");
    for (auto& animationsJson : animationsJsons) {
        endless = quasar::tryGetBool(animationsJson, "endless", false);
        const std::string& url = quasar::getString(animationsJson, "frontal_led_image");
        auto path = urlsToFilenames[url];

        std::shared_ptr<Animation> animation;
        try {
            animation = GifAnimation::readGif(path, ledDevices_->getLedScreen());
        } catch (const std::exception& e) {
            YIO_LOG_ERROR_EVENT("DrawLedScreenDirectiveObserver.ReadGifFailed", "Can't parse gif animation " << path << ", error: " << e.what());
            cacheManager_.invalidateEntry(url);
            device_->telemetry()->reportEvent("frontalAnimationLoadFail " + url, e.what());
            std::remove(path.c_str());
            continue;
        }
        device_->telemetry()->reportEvent("frontalAnimationDownloadSuccess");

        std::shared_ptr<DefaultAnimationComposition> composition = std::make_shared<DefaultAnimationComposition>(
            std::vector<std::shared_ptr<Animation>>{
                std::move(animation), speakingPattern},
            endless);

        compositions.emplace_back(std::move(composition));
    }

    YIO_LOG_INFO("Extracted animations count: " << compositions.size());
    if (compositions.empty()) {
        return std::nullopt;
    }

    tillEndOfSpeech_ = quasar::tryGetBool(payload, "till_end_of_speech", false);

    auto conductor = std::make_shared<DefaultAnimationConductor>(std::move(compositions));
    conductor->setOnFinishedListener([this]() {
        stop();
        if (eventListener_) {
            callbackQueue_.add(eventListener_);
        }
    });
    return conductor;
}

void DrawLedScreenDirectiveObserver::play(std::shared_ptr<DefaultAnimationConductor> conductor) {
    state_ = State::PLAYING;
    activeConductor_ = std::move(conductor);
    if (eventListener_) {
        callbackQueue_.add(eventListener_);
    }
}

void DrawLedScreenDirectiveObserver::stop() {
    activeConductor_.reset();
    state_ = State::STOPPED;
}

bool DrawLedScreenDirectiveObserver::isPlaying() {
    return state_ == State::PLAYING;
}

void DrawLedScreenDirectiveObserver::setEventListener(DrawLedScreenDirectiveObserver::EventListener eventListener) {
    eventListener_ = std::move(eventListener);
}

const std::shared_ptr<DefaultAnimationConductor>& DrawLedScreenDirectiveObserver::getActiveConductor() const {
    return activeConductor_;
}

bool DrawLedScreenDirectiveObserver::hasNoCacheHeader(const std::vector<std::pair<std::string, std::string>>& headers) {
    for (const auto& item : headers) {
        if (item.first == "Cache-Control") {
            if (item.second == "no-store") {
                return true;
            }
        }
    }
    return false;
}

bool DrawLedScreenDirectiveObserver::tillEndOfSpeech() const {
    return tillEndOfSpeech_;
}

void DrawLedScreenDirectiveObserver::setSizeEntries(unsigned long sizeEntries) {
    cacheManager_.setSizeEntries(sizeEntries);
}

void DrawLedScreenDirectiveObserver::setSizeKbytes(int sizeKbytes) {
    cacheManager_.setSizeKbytes(sizeKbytes);
}
