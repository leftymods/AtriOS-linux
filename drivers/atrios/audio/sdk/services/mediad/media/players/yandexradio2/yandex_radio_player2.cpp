#include "yandex_radio_player2.h"

#include <yandex_io/services/mediad/spectrum_listener/spectrum_listener.h>

#include <yandex_io/libs/base/linux_timer.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/telemetry/telemetry.h>
#include <yandex_io/protos/quasar_proto.pb.h>
#include <yandex_io/libs/protobuf_utils/json.h>

#include <atomic>
#include <cmath>
#include <regex>
#include <sstream>
#include <stdexcept>

YIO_DEFINE_LOG_MODULE("media");

using namespace quasar;

namespace {

    constexpr int PROGRESS_HEARTBEAT_PERIOD_SEC = 30;
    constexpr double NORMAL_VOLUME = 1.0;
    constexpr double NO_AUDIO_FOCUS_VOLUME = 0.05;
    constexpr int64_t HEARTBEAT_TIMEOUT_MS{5000};
    constexpr std::chrono::seconds STARTING_TIMEOUT{5};
    constexpr std::chrono::milliseconds BUFFERING_TIMEOUT{7500};
    constexpr std::chrono::milliseconds HEARTBEAT_MONITOR_PULSE{1000};
    constexpr std::chrono::milliseconds VOLUME_CHANGE_PULSE{100};
    constexpr std::chrono::milliseconds VOLUME_CHANGE_NOW{0};
    constexpr std::chrono::milliseconds VOLUME_CHANGE_PERIOD{2000};
    constexpr const char* STATION_VSID_ID = "STM";
    constexpr const char* WHITE_NOISE_GROUP_FIELD = "white_noise_group";
    const std::string WHITE_NOISE_GROUP = "white_noise";
    const std::string LETTERS_AND_DIGITS = "0123456789abcdefghijklmnopqrstuvwxyz";

    int nextWhiteNoiseId()
    {
        static std::atomic<int> whiteNoiseId{0};
        return ++whiteNoiseId;
    }

    // VSID = "QUASAR-5341: <44 случайные цифр/букв>x<3 символа, тип плеера (веб, станция, смарт-тв и др)>x<4 цифры, версия плеера по модулю 10000>x<10 цифр, клиентский таймстамп создания плеера>";
    std::string generateVSID(const std::string softwareVersion)
    {
        std::ostringstream builder;

        for (int i = 0; i < 44; ++i)
        {
            builder << LETTERS_AND_DIGITS[rand() % LETTERS_AND_DIGITS.size()];
        }

        builder << "x" << STATION_VSID_ID << "x";

        std::string version;
        if (!softwareVersion.empty()) {
            std::string digitizedVersion = std::regex_replace(softwareVersion, std::regex(R"([\D])"), "");
            if (digitizedVersion.size() <= 4) {
                version = digitizedVersion + std::string(4 - digitizedVersion.size(), '0');
            } else {
                version = digitizedVersion.substr(digitizedVersion.size() - 4);
            }
        }
        if (version.empty()) {
            version = "0000";
        }
        builder << version << "x";

        auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count() /
                  1000;
        builder << ts;

        YIO_LOG_INFO("VSID is: " << builder.str().c_str());

        return builder.str();
    }

    class AudioListener: public AudioPlayer::SimpleListener {
    public:
        AudioListener(int playerId, YandexRadioPlayer2& owner)
            : playerId_(playerId)
            , owner_(owner)
        {
        }

        void onError(const std::string& message) override {
            owner_.handleError(playerId_, message);
        }

        void onStart() override {
            owner_.handleStart(playerId_);
        }

        void onEnd() override {
            owner_.handleEnd(playerId_);
        }

        void onSeeked() override {
        }

        void onPaused() override {
        }

        void onResumed() override {
            owner_.handleResumed(playerId_);
        }

        void onProgress(int position, int duration) override {
            owner_.handleProgress(playerId_, position, duration);
        }

        void onBufferingStart() override {
            owner_.handleBufferingStart(playerId_);
        }

        void onBufferingEnd() override {
            owner_.handleBufferingEnd(playerId_);
        }

        void onStopped() override {
        }

    private:
        const int playerId_;
        YandexRadioPlayer2& owner_;
    };

} // namespace

YandexRadioPlayer2::YandexRadioPlayer2(std::shared_ptr<YandexIO::IDevice> device,
                                       std::shared_ptr<ipc::IIpcFactory> ipcFactory,
                                       std::shared_ptr<YandexIO::IFilePlayerCapability> filePlayerCapability,
                                       bool ownsFocus,
                                       std::shared_ptr<AudioPlayerFactory> playerFactory)
    : YandexRadioPlayer2(
          std::move(device),
          std::move(ipcFactory),
          std::move(filePlayerCapability),
          ownsFocus,
          std::move(playerFactory),
          Json::Value{Json::objectValue},
          []() {},
          []() {},
          [](Error /*error*/) {})
{
}

YandexRadioPlayer2::YandexRadioPlayer2(
    std::shared_ptr<YandexIO::IDevice> device,
    std::shared_ptr<ipc::IIpcFactory> ipcFactory,
    std::shared_ptr<YandexIO::IFilePlayerCapability> filePlayerCapability,
    bool ownsFocus,
    std::shared_ptr<AudioPlayerFactory> playerFactory,
    const Json::Value& customPlayerConfig,
    OnStateChange onStateChange,
    OnPlayStartChange onStart,
    OnError onError)
    : Player(std::move(device), std::move(onStateChange), std::move(onStart), std::move(onError))
    , filePlayerCapability_(std::move(filePlayerCapability))
    , vsid_(generateVSID(device_->softwareVersion()))
    , playbackConfig_(getJson(device_->configuration()->getServiceConfig("mediad"), "playbackParams"))
    , whiteNoiseGroup_(tryGetString(customPlayerConfig, WHITE_NOISE_GROUP_FIELD, WHITE_NOISE_GROUP))
    , volumeSmoother_(NO_AUDIO_FOCUS_VOLUME)
    , audioCommandQueue_(std::make_shared<NamedCallbackQueue>("YandexRadioPlayer2_audio"))
    , recyclingPlayerQueue_(std::make_shared<NamedCallbackQueue>("YandexRadioPlayer2_recycling"))
    , spectrumProvider_(std::make_shared<YandexIO::SpectrumProvider>(ipcFactory))
{
    YIO_LOG_DEBUG("Create new instance of YandexRadioPlayer2, this=" << this);
    playerFactory_ = std::move(playerFactory);
    type_ = PlayerType::YANDEX_RADIO;

    audioCommandQueue_->add([this]() { acqThreadId_ = std::this_thread::get_id(); });

    if (!ownsFocus) {
        freeAudioFocus();
    }
}

YandexRadioPlayer2::~YandexRadioPlayer2()
{
    YIO_LOG_DEBUG("Destroying instance of YandexRadioPlayer2, this=" << this);
    isPlaying_ = false;
    audioCommandQueue_->add([this]() {
        YIO_LOG_DEBUG("Clean up audio command queue before destroying");
        acqEnsureThread();
        acqDestroyPlayer();
        if (currentWhiteNoiseId_ && !currentWhiteNoiseJson_.empty()) {
            int64_t pauseMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  (std::chrono::steady_clock::now() - currentWhiteNoiseBegin_))
                                  .count();
            currentWhiteNoiseJson_["terminated"] = true;
            currentWhiteNoiseJson_["pauseMs"] = pauseMs;
            reportEvent("yandexRadio2RecoveryFail", currentWhiteNoiseJson_);
        }
        acqDisableWhiteNoise();
    });
    audioCommandQueue_->destroy();
    YIO_LOG_DEBUG("Instance of YandexRadioPlayer2 destroyed, this=" << this);
}

void YandexRadioPlayer2::play(const Json::Value& options)
{
    YIO_LOG_INFO("Radio play");
    reportEvent("yandexRadio2PlayRequest");
    {
        std::unique_lock<std::mutex> lock(mutex_);
        isPlaying_ = true;
        options_ = options;
        url_ = addGETParam(options["url"].asString(), "vsid", vsid_);
        vinsInitRequestId_ = options["vins_init_request_id"].isNull() ? "" : options["vins_init_request_id"].asString();
        if (!vinsInitRequestId_.empty()) {
            lastPlayCommandTs_ = getNowTimestampMs();
        }

        int playerId = ++playerId_;
        auto url = url_;
        audioCommandQueue_->add([this, playerId, url]() { acqPlay(playerId, url); });
    }

    playRequestLatencyPoint_ = device_->telemetry()->createLatencyPoint();

    notifyStateChanged();
}

void YandexRadioPlayer2::pause(bool smooth, const std::string& /*vinsRequestId*/)
{
    YIO_LOG_DEBUG("Pause request");
    reportEvent("yandexRadio2PauseRequest");
    {
        std::unique_lock<std::mutex> lock(mutex_);
        pauseUnsafe(smooth);
    }
    notifyStateChanged();
}

void YandexRadioPlayer2::discard()
{
    YIO_LOG_DEBUG("Discard request");
    {
        std::unique_lock<std::mutex> lock(mutex_);
        isPlaying_ = false;
        audioCommandQueue_->add([this]() { acqDestroyPlayer(); });
    }
    notifyStateChanged();
}

void YandexRadioPlayer2::resume(bool smooth, const std::string& /*vinsRequestId*/)
{
    YIO_LOG_DEBUG("Resume request");
    {
        std::unique_lock<std::mutex> lock(mutex_);
        resumeUnsafe(smooth);
    }
    notifyStateChanged();
}

bool YandexRadioPlayer2::isPlaying() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return isPlaying_;
}

void YandexRadioPlayer2::seekTo(int ms)
{
    YIO_LOG_INFO("Radio seek to: " << ms << " ms");
    std::unique_lock<std::mutex> lock(mutex_);
    audioCommandQueue_->add(
        [this, ms]() {
            acqEnsureThread();
            if (currentPlayer_) {
                YIO_LOG_DEBUG("Invoke seek to " << ms << " ms");
                currentPlayer_->seek(ms);
            }
        });
}

void YandexRadioPlayer2::seekForward(int /* seconds */)
{
    YIO_LOG_ERROR_EVENT("YandexRadioPlayer2.NotImplemented.SeekForward", "Not supported");
}

void YandexRadioPlayer2::seekBackward(int /* seconds */)
{
    YIO_LOG_ERROR_EVENT("YandexRadioPlayer2.NotImplemented.SeekBackward", "Not supported");
}

void YandexRadioPlayer2::takeAudioFocus(bool smooth)
{
    YIO_LOG_INFO("Radio take audio focus: smooth= " << smooth);

    hasAudioFocus_ = true;

    std::unique_lock<std::mutex> lock(mutex_);
    if (isPlaying_) {
        setVolume(NORMAL_VOLUME, smooth ? VOLUME_CHANGE_PERIOD : VOLUME_CHANGE_NOW);
    }
}

void YandexRadioPlayer2::freeAudioFocus()
{
    YIO_LOG_INFO("Radio free audio focus");

    hasAudioFocus_ = false;
    setVolume(NO_AUDIO_FOCUS_VOLUME, VOLUME_CHANGE_NOW);
}

bool YandexRadioPlayer2::hasAudioFocus() const {
    return hasAudioFocus_;
}

void YandexRadioPlayer2::setEqualizerConfig(const YandexIO::EqualizerConfig& config) {
    audioCommandQueue_->add([this, config] {
        equalizerConfig_ = config;

        if (currentPlayer_) {
            YIO_LOG_INFO("YandexRadioPlayer2::setEqualizerConfig");
            currentPlayer_->setEqualizerConfig(config);
        }
    });
}

void YandexRadioPlayer2::processCommand(const std::string& /* command */, const Json::Value& /* options */)
{
    YIO_LOG_ERROR_EVENT("YandexRadioPlayer2.NotImplemented.ProcessCommand", "Not supported");
}

YandexRadioPlayer2::ChangeConfigResult YandexRadioPlayer2::updateConfig(const Json::Value& config)
{
    if (tryGetString(config, WHITE_NOISE_GROUP_FIELD, WHITE_NOISE_GROUP) != whiteNoiseGroup_) {
        return ChangeConfigResult::NEED_RECREATE;
    }

    return ChangeConfigResult::NO_CHANGES;
}

Player::State YandexRadioPlayer2::getState()
{
    std::lock_guard lock(mutex_);

    NAlice::TDeviceState deviceState;
    deviceState.MutableRadio()->CopyFrom(buildRadioState());

    return State{
        .deviceState = deviceState,
        .vinsRequestId = vinsInitRequestId_};
}

std::string YandexRadioPlayer2::getTrackIdInternal() const {
    std::unique_lock<std::mutex> lock(mutex_);
    return url_;
}

void YandexRadioPlayer2::handleError(int playerId, const std::string& error)
{
    std::unique_lock<std::mutex> lock(mutex_);
    audioCommandQueue_->add([this, playerId, error]() { acqProcessUnexpectedPause(playerId, PauseType::RESTARTING, error); });
}

void YandexRadioPlayer2::handleStart(int playerId)
{
    heartbeat_ = heartbeat();
    std::unique_lock<std::mutex> lock(mutex_);
    audioCommandQueue_->add(
        [this, playerId]()
        {
            if (playerId != currentPlayerId_) {
                return;
            }
            device_->telemetry()->reportLatency(std::move(playRequestLatencyPoint_), "yandexRadio2Started");

            currentFirstRun_ = false;
            acqCancelUnexpectedPause(playerId, "Handle onStart event");
            if (onStart_) {
                onStart_();
            }
        });
    audioCommandQueue_->addDelayed([this, playerId] { acqHeartbeatMonitor(playerId); }, HEARTBEAT_MONITOR_PULSE);
}

void YandexRadioPlayer2::handleEnd(int playerId)
{
    std::unique_lock<std::mutex> lock(mutex_);
    audioCommandQueue_->add([this, playerId]() { acqProcessUnexpectedPause(playerId, PauseType::RESTARTING, "Received end of media stream"); });
}

void YandexRadioPlayer2::handleResumed(int playerId)
{
    heartbeat_ = heartbeat();

    std::unique_lock<std::mutex> lock(mutex_);
    audioCommandQueue_->add([this, playerId]() { acqCancelUnexpectedPause(playerId, "Handle onResumed event"); });
}

void YandexRadioPlayer2::handleProgress(int playerId, int position, int /* duration */)
{
    heartbeat_ = heartbeat();

    std::unique_lock<std::mutex> lock(mutex_);
    audioCommandQueue_->add(
        [this, playerId, position]()
        {
            if (playerId != currentPlayerId_) {
                return;
            }
            if (currentReportedPosition_ < 0 ||
                position < currentReportedPosition_ ||
                position - currentReportedPosition_ >= PROGRESS_HEARTBEAT_PERIOD_SEC)
            {
                reportEvent("progressHeartbeatRadio2");
                currentReportedPosition_ = position;
            }
        });
}

void YandexRadioPlayer2::handleBufferingStart(int playerId)
{
    std::unique_lock<std::mutex> lock(mutex_);
    audioCommandQueue_->add(
        [this, playerId]()
        {
            if (playerId != currentPlayerId_) {
                return;
            }
            if (!currentFirstRun_) {
                if (currentPlayer_) {
                    currentPlayer_->pause();
                }
                acqProcessUnexpectedPause(playerId, PauseType::AWAITING, "Buffering too long during playback");
            }
        });
}

void YandexRadioPlayer2::handleBufferingEnd(int playerId)
{
    heartbeat_ = heartbeat();

    std::unique_lock<std::mutex> lock(mutex_);
    audioCommandQueue_->add(
        [this, playerId]()
        {
            if (playerId != currentPlayerId_) {
                return;
            }
            if (currentPlayer_) {
                currentPlayer_->playAsync();
            }
            acqCancelUnexpectedPause(playerId, "Handle onBufferingEnd event");
        });
}

void YandexRadioPlayer2::pauseUnsafe(bool /* smooth */)
{
    // mutex_ must be locked!
    YIO_LOG_INFO("Radio pause");
    pauseRequestLatencyPoint_ = device_->telemetry()->createLatencyPoint();
    isPlaying_ = false;
    setVolume(NO_AUDIO_FOCUS_VOLUME, VOLUME_CHANGE_NOW);
    audioCommandQueue_->add([this]() { acqPause(); });
}

void YandexRadioPlayer2::resumeUnsafe(bool /* smooth */)
{
    // mutex_ must be locked!
    if (url_.empty()) {
        YIO_LOG_WARN("It is impossible to continue playing, because the radio was not running");
        return;
    }
    isPlaying_ = true;
    int playerId = playerId_;
    auto url = url_;
    audioCommandQueue_->add([this, playerId, url]() { acqPlay(playerId, url); });
}

void YandexRadioPlayer2::setVolume(float targetVolume, std::chrono::milliseconds period)
{
    volumeSmoother_.setVolume(targetVolume, period);

    bool expected = false;
    if (volumeSmoothing_.compare_exchange_strong(expected, true)) {
        /*
         * Here mutex_ is not used since it doesn't matter to us in
         * what order the sound change will be performed, the main thing
         * is to start the cyclic of uniform sound change
         */
        audioCommandQueue_->addDelayed([this]() { acqUpdateVolume(); }, VOLUME_CHANGE_PULSE);
    }
}

void YandexRadioPlayer2::reportError(ErrorLevel errorLevel, const std::string& errorMessage) const {
    Json::Value errorJson;
    errorJson["message"] = errorMessage;
    errorJson["radio2"] = true;

    switch (errorLevel) {
        default:
        case ErrorLevel::ERROR:
            YIO_LOG_ERROR_EVENT("YandexRadioPlayer2.ReportError", "Radio: " << errorMessage);
            errorJson["error_level"] = "ERROR";
            break;
        case ErrorLevel::FATAL:
            YIO_LOG_ERROR_EVENT("YandexRadioPlayer2.ReportFatal", "Radio: " << errorMessage);
            errorJson["error_level"] = "FATAL";
            break;
    }
    reportEvent("yandexRadio2Error", errorJson);
}

void YandexRadioPlayer2::notifyStateChanged() const {
    if (onStateChange_) {
        onStateChange_();
    }
}

int64_t YandexRadioPlayer2::heartbeat()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void YandexRadioPlayer2::acqEnsureThread() const {
    if (acqThreadId_ != std::this_thread::get_id()) {
        Json::Value json;
        json["message"] = "Method for working with media stream is called from the wrong thread";
        reportEvent("yandexRadio2InternalError", json);
        throw std::logic_error(json["message"].asString());
    }
}

void YandexRadioPlayer2::acqDestroyPlayer()
{
    acqEnsureThread();

    if (currentPlayer_) {
        YIO_LOG_DEBUG("Destroying internal gstreamer player: playerId=" << currentPlayerId_);
        if (currentPlayer_->isPlaying()) {
            currentPlayer_->pause();
        }
        auto currentPlayerId = currentPlayerId_;
        currentPlayer_->removeListeners();
        currentPlayerId_ = 0;
        currentFirstRun_ = false;
        recyclingPlayerQueue_->add(
            [currentPlayerId, ptr = std::shared_ptr<AudioPlayer>(currentPlayer_.release())]() mutable {
                YIO_LOG_DEBUG("Destroing instance of audio player in recycling thread, playerId=" << currentPlayerId);
                ptr.reset();
                YIO_LOG_DEBUG("Instance of audio player in recycling thread destroyed, playerId=" << currentPlayerId);
            });
        YIO_LOG_DEBUG("Internal gstreamer player sent for recycling");
    }
}

void YandexRadioPlayer2::acqPlay(int playerId, const std::string& url)
{
    Y_VERIFY(playerId > currentPlayerId_);

    acqEnsureThread();

    YIO_LOG_DEBUG("Prepare new player to play a radio");
    acqDestroyPlayer();

    currentPlayer_ = acqCreatePlayer(playerId, url);
    currentPlayerId_ = playerId;
    currentFirstRun_ = true;
    currentReportedPosition_ = -1;
    heartbeat_ = heartbeat();
    acqDisableWhiteNoise();

    acqUpdateVolume(); // Set current volume level before start playing
    YIO_LOG_DEBUG("Starting playing a radio url=" << currentPlayer_->params().uri());
    currentPlayer_->playAsync();
    YIO_LOG_DEBUG("Radio started");
    if (hasAudioFocus_) {
        setVolume(NORMAL_VOLUME, VOLUME_CHANGE_PERIOD);
    }
    audioCommandQueue_->addDelayed(
        [this, playerId]()
        {
            // Long start watch dog
            // We give X seconds of silence so that the radio has time to start playing, if it still cannot, then turn on the white noise
            if (playerId != currentPlayerId_ || currentFirstRun_ == false) {
                return;
            }
            acqProcessUnexpectedPause(playerId, PauseType::AWAITING, "Radio starting too long");
        }, STARTING_TIMEOUT);
}

void YandexRadioPlayer2::acqPause()
{
    acqEnsureThread();

    if (!currentPlayer_) {
        return;
    }

    acqDestroyPlayer();
    acqDisableWhiteNoise();
    device_->telemetry()->reportLatency(std::move(pauseRequestLatencyPoint_), "yandexRadio2Paused");
}

void YandexRadioPlayer2::acqUpdateVolume()
{
    acqEnsureThread();

    auto volume = volumeSmoother_.currentVolume();
    if (currentPlayer_) {
        currentPlayer_->setVolume(std::sqrt(volume));
    }
    volumeSmoothing_ = false;

    if (volume != volumeSmoother_.targetVolume()) {
        bool expected = false;
        if (volumeSmoothing_.compare_exchange_strong(expected, true)) {
            /*
             * Here mutex_ is not used since it doesn't matter to us in
             * what order the sound change will be performed, the main thing
             * is to continue the cyclic of uniform sound change
             */
            audioCommandQueue_->addDelayed([this]() { acqUpdateVolume(); }, VOLUME_CHANGE_PULSE);
        }
    }
}

std::unique_ptr<AudioPlayer> YandexRadioPlayer2::acqCreatePlayer(int playerId, const std::string& url)
{
    acqEnsureThread();

    YIO_LOG_DEBUG("Create internal gstreamer player playerId=" << playerId);
    const auto playerParams = playerFactory_->createParams()->fromConfig(playbackConfig_).setURI(url);
    std::unique_ptr<AudioPlayer> currentPlayer = playerFactory_->createPlayer(playerParams);
    currentPlayer->addListener(std::make_shared<SpectrumListener>(spectrumProvider_));
    currentPlayer->addListener(std::make_shared<AudioListener>(playerId, *this));
    currentPlayer->setEqualizerConfig(equalizerConfig_);
    return currentPlayer;
}

void YandexRadioPlayer2::acqProcessUnexpectedPause(int playerId, PauseType pauseType, const std::string& errorMessage)
{
    acqEnsureThread();

    if (playerId != currentPlayerId_) {
        return;
    }

    bool justRetry = (currentWhiteNoiseId_ > 0);
    if (!justRetry) {
        Json::Value errorJson;
        errorJson["message"] = errorMessage;
        errorJson["pause_type"] = (pauseType == PauseType::RESTARTING ? "RESTARTING" : "AWATING");
        acqEnableWhiteNoise(nextWhiteNoiseId(), pauseType, std::move(errorJson));
    }

    if (pauseType == PauseType::RESTARTING) {
        YIO_LOG_DEBUG("Restarting radio player: " << errorMessage);
        acqRestartPlayer(playerId);
    } else {
        YIO_LOG_DEBUG("Awaiting radio signal: " << errorMessage);
    }

    if (justRetry) {
        currentWhiteNoiseJson_["retry_count"] = tryGetInt(currentWhiteNoiseJson_, "retry_count", 1) + 1;
        return;
    }
}

void YandexRadioPlayer2::acqRestartPlayer(int playerId)
{
    acqDestroyPlayer();

    int newPlayerId = 0;
    bool isPlaying = false;
    std::string url;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (url_.empty() || playerId != playerId_) {
            // Someone has already created a newer player, so you can forget about it
            return;
        }
        newPlayerId = ++playerId_;
        isPlaying = isPlaying_;
        url = url_;
    }

    currentPlayer_ = acqCreatePlayer(newPlayerId, url);
    currentPlayerId_ = newPlayerId;
    currentReportedPosition_ = -1;
    playerId = newPlayerId;
    currentWhiteNoiseJson_["restarts"] = tryGetInt(currentWhiteNoiseJson_, "restarts", 0) + 1;
    if (isPlaying) {
        currentPlayer_->playAsync();
    }
}

void YandexRadioPlayer2::acqCancelUnexpectedPause(int playerId, const std::string& reason)
{
    acqEnsureThread();

    if (playerId != currentPlayerId_) {
        return;
    }

    heartbeat_ = heartbeat();

    if (currentWhiteNoiseId_) {
        if (!currentWhiteNoiseJson_.empty()) {
            int64_t pauseMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  (std::chrono::steady_clock::now() - currentWhiteNoiseBegin_))
                                  .count();
            YIO_LOG_INFO("Continue radio translation after " << pauseMs << " ms: " << reason);
            currentWhiteNoiseJson_["continue"] = reason;
            currentWhiteNoiseJson_["pauseMs"] = pauseMs;
            reportEvent("yandexRadio2RecoverySuccess", currentWhiteNoiseJson_);
        }
        acqDisableWhiteNoise();
    }
}

void YandexRadioPlayer2::acqAbortUnexpectedPause(int whiteNoiseId)
{
    acqEnsureThread();

    if (whiteNoiseId &&
        whiteNoiseId != currentWhiteNoiseId_)
    {
        return;
    }

    if (!currentWhiteNoiseJson_.empty()) {
        int64_t pauseMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                              (std::chrono::steady_clock::now() - currentWhiteNoiseBegin_))
                              .count();
        YIO_LOG_ERROR_EVENT("YandexRadioPlayer2.FailedRecover", "Radio: Fail to continue radio translation after " << pauseMs << " ms: " << currentWhiteNoiseJson_["message"].asString());
        currentWhiteNoiseJson_["pauseMs"] = pauseMs;
        reportEvent("yandexRadio2RecoveryFail", currentWhiteNoiseJson_);
    }

    auto currentPlayerId = currentPlayerId_;

    acqDestroyPlayer();
    acqDisableWhiteNoise();
    acqSayLostTheWave();

    bool notifyFlag = false;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (currentPlayerId == playerId_) {
            isPlaying_ = false;
            notifyFlag = true;
        }
    }
    YIO_LOG_DEBUG("Abort unexpected pause processed, send notification notifyFlag=" << notifyFlag);
    if (notifyFlag) {
        notifyStateChanged();
    }
}

void YandexRadioPlayer2::acqEnableWhiteNoise(int whiteNoiseId, PauseType pauseType, Json::Value whiteNoiseJson)
{
    acqDisableWhiteNoise();

    YIO_LOG_DEBUG("Enable white noise whiteNoiseId=" << whiteNoiseId);

    currentWhiteNoiseId_ = whiteNoiseId;
    currentWhiteNoiseJson_ = std::move(whiteNoiseJson);
    currentWhiteNoiseBegin_ = std::chrono::steady_clock::now();

    audioCommandQueue_->addDelayed(
        [this, whiteNoiseId, playerId = currentPlayerId_, pauseType]()
        {
            if (whiteNoiseId == currentWhiteNoiseId_ && pauseType == PauseType::AWAITING) {
                YIO_LOG_DEBUG("Try restart player before really fail");
                acqRestartPlayer(playerId);
                audioCommandQueue_->addDelayed([this, whiteNoiseId] { acqAbortUnexpectedPause(whiteNoiseId); }, BUFFERING_TIMEOUT);
                return;
            }
            acqAbortUnexpectedPause(whiteNoiseId);
        }, BUFFERING_TIMEOUT);

    YandexIO::IFilePlayerCapability::PlayParams playParams{
        .playTimes = 6 // If the Internet is lost, the player may take a long time to shut down, so just in case we’ll do a few white noise cycles
    };
    filePlayerCapability_->playSoundFile("white_noise.wav", std::nullopt, std::move(playParams));
}

void YandexRadioPlayer2::acqDisableWhiteNoise()
{
    if (currentWhiteNoiseId_) {
        YIO_LOG_DEBUG("Stop white noise whiteNoiseId=" << currentWhiteNoiseId_);
        currentWhiteNoiseId_ = 0;
        currentWhiteNoiseJson_ = Json::Value{Json::nullValue};

        filePlayerCapability_->stopSoundFile("white_noise.wav");
    } else {
        YIO_LOG_DEBUG("No white noise");
    }
}

void YandexRadioPlayer2::acqSayLostTheWave()
{
    YIO_LOG_DEBUG("Play \"radio_error.wav\" to say \"I'm lost the wave, sorry\"");
    filePlayerCapability_->playSoundFile("radio_error.wav", proto::AudioChannel::DIALOG_CHANNEL);
}

void YandexRadioPlayer2::acqHeartbeatMonitor(int playerId)
{
    acqEnsureThread();

    if (playerId != currentPlayerId_) {
        return;
    }

    if (isPlaying() && heartbeat() - heartbeat_ > HEARTBEAT_TIMEOUT_MS && !currentWhiteNoiseId_) {
        acqProcessUnexpectedPause(playerId, PauseType::AWAITING, "Radio shows no signs of life");
    }
    audioCommandQueue_->addDelayed([this, playerId] { acqHeartbeatMonitor(playerId); }, HEARTBEAT_MONITOR_PULSE);
}

google::protobuf::Struct YandexRadioPlayer2::buildRadioState() const {
    Json::Value jsonState;
    jsonState["vins_init_request_id"] = vinsInitRequestId_;

    Json::Value currentlyPlaying = Json::objectValue;
    currentlyPlaying["radioId"] = options_["id"].asString();
    currentlyPlaying["radioTitle"] = options_["title"].asString();
    currentlyPlaying["last_play_timestamp"] = lastPlayCommandTs_;
    jsonState["currently_playing"] = std::move(currentlyPlaying);

    Json::Value player = Json::objectValue;
    player["pause"] = !isPlaying_;
    if (!isPlaying_) {
        player["timestamp"] = getNowMs() / 1000;
    }
    jsonState["player"] = std::move(player);

    jsonState["playlist_owner"] = options_["uid"].asString();
    jsonState["last_play_timestamp"] = lastPlayCommandTs_;
    jsonState["color"] = options_["color"].asString();
    jsonState["image_url"] = options_["image_url"].asString();

    auto state = convertJsonToProtobuf<google::protobuf::Struct>(quasar::jsonToString(jsonState));
    return state.value_or(google::protobuf::Struct());
}