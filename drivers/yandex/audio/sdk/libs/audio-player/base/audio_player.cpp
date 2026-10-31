#include "audio_player.h"

#include "utils.h"

#include <contrib/libs/fmt/include/fmt/format.h>

#include <json/json.h>

#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

using namespace quasar;

namespace {

    std::string getNormalizationPluginsValue(const std::optional<AudioPlayer::Params::Normalization>& n) {
        if (!n.has_value()) {
            return "identity"; // fake element when normalization is not set
        }

        std::stringstream ss;
        ss << "volume name=normalization volume=";
        ss << std::fixed << std::setprecision(2) << calcGstNormalizationVolume(*n);
        return ss.str();
    }

} // namespace

AudioPlayer::Channel AudioPlayer::parseChannel(std::string_view text)
{
    if (text == "default" || text == "" || text == "all") {
        return Channel::ALL;
    } else if (text == "left") {
        return Channel::LEFT;
    } else if (text == "right") {
        return Channel::RIGHT;
    } else {
        return Channel::UNDEFINED;
    }
}

const char* AudioPlayer::channelName(Channel channel)
{
    switch (channel) {
        case Channel::ALL:
            return "all";
        case Channel::LEFT:
            return "left";
        case Channel::RIGHT:
            return "right";
        case Channel::UNDEFINED:
            break;
    }
    return "undefined";
}

AudioPlayer::~AudioPlayer() {
    commandsQueue_.destroy();
}

bool AudioPlayer::playMultiroom(std::chrono::nanoseconds /* basetime */, std::chrono::nanoseconds /* position */)
{
    sendError("Multiroom mode support for this player is not implemented");
    return false;
}

AudioPlayer::Channel AudioPlayer::channel() {
    return Channel::UNDEFINED;
}

bool AudioPlayer::setChannel(AudioPlayer::Channel /*ch*/) {
    return false;
}

void AudioPlayer::addListener(std::shared_ptr<AudioPlayer::Listener> listener) {
    commandsQueue_.add([this, listener]() {
        listeners_.push_back(listener);
    });
}

void AudioPlayer::removeListeners() {
    commandsQueue_.add([this]() {
        listeners_.clear();
    });
}

void AudioPlayer::sendEnd() const {
    commandsQueue_.add([this]() {
        for (const auto& listener : listeners_) {
            listener->onEnd();
        }
    });
}

void AudioPlayer::sendStart() const {
    commandsQueue_.add([this]() {
        for (const auto& listener : listeners_) {
            listener->onStart();
        }
    });
}

void AudioPlayer::sendSeeked() const {
    commandsQueue_.add([this]() {
        for (const auto& listener : listeners_) {
            listener->onSeeked();
        }
    });
}

void AudioPlayer::sendPaused() const {
    commandsQueue_.add([this]() {
        for (const auto& listener : listeners_) {
            listener->onPaused();
        }
    });
}

void AudioPlayer::sendResumed() const {
    commandsQueue_.add([this]() {
        for (const auto& listener : listeners_) {
            listener->onResumed();
        }
    });
}

void AudioPlayer::sendError(const std::string& message) const {
    commandsQueue_.add([this, message]() {
        for (const auto& listener : listeners_) {
            listener->onError(message);
        }
    });
}

void AudioPlayer::sendProgress() const {
    commandsQueue_.add([this]() {
        for (const auto& listener : listeners_) {
            listener->onProgress(position(), duration());
        }
    });
}

void AudioPlayer::sendBufferingStart() const {
    commandsQueue_.add([this]() {
        for (const auto& listener : listeners_) {
            listener->onBufferingStart();
        }
    });
}

void AudioPlayer::sendBufferingEnd() const {
    commandsQueue_.add([this]() {
        for (const auto& listener : listeners_) {
            listener->onBufferingEnd();
        }
    });
}

void AudioPlayer::sendStopped() const {
    commandsQueue_.add([this]() {
        for (const auto& listener : listeners_) {
            listener->onStopped();
        }
    });
}

void AudioPlayer::sendSpectrum(const Listener::SpectrumFrame& frame) const {
    commandsQueue_.add([this, frame]() {
        for (const auto& listener : listeners_) {
            listener->onSpectrum(frame);
        }
    });
}

void AudioPlayer::sendBufferStalled() {
    commandsQueue_.add([this]() {
        if (++bufferStalledEvents_ % params_.bufferStalledThrottle() == 0) {
            for (const auto& listener : listeners_) {
                listener->onBufferStalled();
            }
        }
    });
}

AudioPlayer::Params& AudioPlayer::Params::fromConfig(const Json::Value& config) {
    if (config.isMember("deviceName")) {
        setAlsaDevice(config["deviceName"].asString());
    }
    if (config.isMember("streamMode")) {
        isStreamMode_ = config["streamMode"].asBool();
    }
    if (config.isMember("sinkOptions")) {
        setSinkOptionsJson(config["sinkOptions"]);
    }
    if (config.isMember("bufferStalledThrottle") && config["bufferStalledThrottle"].isInt()) {
        bufferStalledThrottle_ = config["bufferStalledThrottle"].asInt();
        if (bufferStalledThrottle_ <= 0) {
            bufferStalledThrottle_ = 1;
        }
    }

    if (config.isMember("default_volume_element")) {
        defaultVolumeElement_ = config["default_volume_element"].asString();
    }

    if (config.isMember("channel")) {
        auto key = config["channel"].asString();
        channel_ = parseChannel(key);
        if (channel_ == Channel::UNDEFINED) {
            throw std::runtime_error("Unsupported channel name \"" + key + "\"");
        }
    }

    initGstreamerPipeline(config);

    return *this;
}

void AudioPlayer::Params::initGstreamerPipeline(const Json::Value& config) {
    if (config.isMember("gstPipeline")) {
        setGstreamerPipeline(config["gstPipeline"].asString());
    }
}

AudioPlayer::Params& AudioPlayer::Params::setAlsaDevice(const std::string& deviceName, int cardNumber, int deviceNumber) {
    std::stringstream ss;
    ss << deviceName << ":" << cardNumber << "," << deviceNumber;
    return setAlsaDevice(ss.str());
}

AudioPlayer::Params& AudioPlayer::Params::setAlsaDevice(const std::string& deviceName) {
    deviceName_ = deviceName;
    return *this;
}

AudioPlayer::Params& AudioPlayer::Params::setInitialSeekMs(int ms) {
    initialSeekMs_ = ms;
    return *this;
}

AudioPlayer::Params& AudioPlayer::Params::setFilePath(const std::string& path) {
    filePath_ = path;
    return setURI("file://" + filePath_);
}

AudioPlayer::Params& AudioPlayer::Params::setURI(const std::string& uri) {
    uri_ = uri;
    return setGstreamerPipeline(gstPipeline_);
}

AudioPlayer::Params& AudioPlayer::Params::setSoupHttpSrcConfig(const std::map<std::string, std::string>& config) {
    soupHttpSrcConfig_ = config;
    setGstreamerPipeline(gstPipeline_);
    return *this;
}

AudioPlayer::Params& AudioPlayer::Params::setSinkOptionsJson(const Json::Value& options) {
    std::map<std::string, std::string> optionsMap;
    for (auto it = options.begin(); it != options.end(); it++) {
        optionsMap.emplace(it.name(), it->asString());
    }
    return setSinkOptions(optionsMap);
}

AudioPlayer::Params& AudioPlayer::Params::setSinkOptions(const std::map<std::string, std::string>& options) {
    sinkOptions_ = options;
    return *this;
}

AudioPlayer::Params& AudioPlayer::Params::setAudioClock(std::shared_ptr<const AudioClock> audioClock) {
    audioClock_ = std::move(audioClock);
    return *this;
}

AudioPlayer::Params& AudioPlayer::Params::setMode(Mode mode) {
    mode_ = mode;
    return *this;
}

AudioPlayer::Params& AudioPlayer::Params::setChannel(Channel channel) {
    channel_ = channel;
    setGstreamerPipeline(gstPipeline_);
    return *this;
}

AudioPlayer::Params& AudioPlayer::Params::setIsStreamMode(bool isStreamMode) {
    isStreamMode_ = isStreamMode;
    return *this;
}

AudioPlayer::Params& AudioPlayer::Params::setNormalization(const Normalization& settings) {
    normalization_ = settings;
    setGstreamerPipeline(gstPipeline_);
    return *this;
}

AudioPlayer::Params& AudioPlayer::Params::setGstreamerPipeline(const std::string& pipeline) {
    gstPipeline_ = pipeline;
    const std::string normalization = getNormalizationPluginsValue(normalization_);
    std::string volumeElement = defaultVolumeElement_;
    if (streamSrc_ && streamSrc_->useVolumeElementStub()) {
        // use fake volume element
        volumeElement = "identity";
    }
    gstPipelineProcessed_ = fmt::format(
        gstPipeline_, // FIXME: arbitrary format strings are usually rather insecure
        fmt::arg("uri", escapeGstString(uri_)),
        fmt::arg("file_path", escapeGstString(filePath_)),
        fmt::arg("deviceName", escapeGstString(deviceName_)),
        fmt::arg("souphttpsrc_config", serializeGstOptions(soupHttpSrcConfig_)),
        fmt::arg("sink_options", serializeGstOptions(sinkOptions_)),
        fmt::arg("input_media_type", inputStreamMediaType_),
        fmt::arg("opt_volume", volumeElement),
        fmt::arg("opt_normalization", normalization));
    return *this;
}

AudioPlayer::Params& AudioPlayer::Params::setStreamSrc(StreamSrcPtr streamSrc) {
    streamSrc_ = std::move(streamSrc);
    if (streamSrc_) {
        inputStreamMediaType_ = streamSrc_->getMediaTypeString();
    }
    setGstreamerPipeline(gstPipeline_);
    return *this;
}

const std::string& AudioPlayer::Params::alsaDevice() const {
    return deviceName_;
}

const std::optional<AudioPlayer::Params::Normalization>& AudioPlayer::Params::normalization() const {
    return normalization_;
}

const std::string& AudioPlayer::Params::uri() const {
    return uri_;
}

const std::string& AudioPlayer::Params::filePath() const {
    return filePath_;
}

const std::string& AudioPlayer::Params::gstPipeline() const {
    return gstPipeline_;
}

const std::string& AudioPlayer::Params::gstPipelineProcessed() const {
    return gstPipelineProcessed_;
}

bool AudioPlayer::Params::onShotMode() const {
    return onShotMode_;
}

int AudioPlayer::Params::initialOffsetMs() const {
    return initialSeekMs_;
}

std::shared_ptr<const AudioClock> AudioPlayer::Params::audioClock() const {
    return audioClock_;
}

AudioPlayer::Params::Mode AudioPlayer::Params::mode() const {
    return mode_;
}

AudioPlayer::Channel AudioPlayer::Params::channel() const {
    return channel_;
}

StreamSrcPtr AudioPlayer::Params::streamSrc() const {
    return streamSrc_;
}

bool AudioPlayer::Params::isStreamMode() const {
    return isStreamMode_;
}

int AudioPlayer::Params::bufferStalledThrottle() const {
    return bufferStalledThrottle_;
}

void AudioPlayer::SimpleListener::onError(const std::string& /* message */) {
}

void AudioPlayer::SimpleListener::onPaused() {
}

void AudioPlayer::SimpleListener::onResumed() {
}

void AudioPlayer::SimpleListener::onStart() {
}

void AudioPlayer::SimpleListener::onEnd() {
}

void AudioPlayer::SimpleListener::onSeeked() {
}

void AudioPlayer::SimpleListener::onProgress(int /* position */, int /* duration */) {
}

void AudioPlayer::SimpleListener::onBufferingStart() {
}

void AudioPlayer::SimpleListener::onBufferingEnd() {
}

void AudioPlayer::SimpleListener::onStopped() {
}

void AudioPlayer::SimpleListener::onSpectrum(const SpectrumFrame& /* frame */) {
}

void AudioPlayer::SimpleListener::onBufferStalled() {
}

AudioListenerInjector::AudioListenerInjector(
    std::unique_ptr<AudioPlayerFactory> playerFactory,
    ListenerFactory listenerFactory)
    : playerFactory_(std::move(playerFactory))
    , listenerFactory_(std::move(listenerFactory))
{
}

std::unique_ptr<AudioPlayer> AudioListenerInjector::createPlayer(const AudioPlayer::Params& params) {
    auto player = playerFactory_->createPlayer(params);
    auto listeners = (listenerFactory_ ? listenerFactory_() : std::vector<std::shared_ptr<AudioPlayer::Listener>>{});
    for (auto& listener : listeners) {
        player->addListener(std::move(listener));
    }
    return player;
}

std::unique_ptr<AudioPlayer::Params> AudioListenerInjector::createParams() {
    return playerFactory_->createParams();
}

void AudioListenerInjector::configUpdated(const Json::Value& config) {
    playerFactory_->configUpdated(config);
}
