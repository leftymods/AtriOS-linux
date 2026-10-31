#include "spectrogram_animation.h"

#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/protobuf_utils/json.h>

#include <yandex_io/protos/quasar_proto.pb.h>

#include <algorithm>
#include <iomanip>

YIO_DEFINE_LOG_MODULE("leds");

namespace {

    constexpr float THRESHOLD = -80.0f; // XXX pass with spectrum

    constexpr size_t MAX_BUFFER_SIZE = 125; // 5s

    std::chrono::steady_clock::time_point convertTime(std::chrono::nanoseconds time) {
        auto steadyNow = std::chrono::steady_clock::now();
        auto systemNow = std::chrono::system_clock::now();
        return steadyNow + (time - systemNow.time_since_epoch());
    }

} // namespace

SpectrogramAnimation::SpectrogramAnimation(
    std::shared_ptr<quasar::ipc::IIpcFactory> ipcFactory,
    std::weak_ptr<quasar::LedDevice> ledDevice,
    std::chrono::milliseconds frameInterval)
    : Animation(ledDevice)
    , frameInterval_(frameInterval)
    , isPaused_(true)
{
    if (ipcFactory) {
        server_ = ipcFactory->createIpcServer("spectrogram_animation");
        server_->setClientConnectedHandler([this](auto& /* connection */) {
            std::lock_guard lock(mutex_);
            sendStateUnsafe(true);
        });
        server_->setMessageHandler([this](const auto& message, auto& /* connection */) {
            if (message->has_spectrogram_animation_message() &&
                message->spectrogram_animation_message().has_set_external_presets_signal()) {
                const auto& seps = message->spectrogram_animation_message().set_external_presets_signal();
                updateExternalPresets(seps.configs(), seps.current(), seps.extra_data());
            }
        });
        server_->listenService();
    }
}

SpectrogramAnimation::~SpectrogramAnimation() {
    if (server_) {
        server_->shutdown();
    }
}

void SpectrogramAnimation::updateAndDrawCanvas() {
    std::scoped_lock guard(mutex_);

    if (auto bands = updateSpectrum()) {
        drawSpectrogramCanvas(std::move(*bands));
    } else {
        drawSpectrogramCanvas(std::vector<float>(getSpectrogram()->getBandCount()));
    }
    sendStateUnsafe(false);
}

bool SpectrogramAnimation::finished() const {
    return false;
}

Animation::TimePoint SpectrogramAnimation::getEndOfFrameTimePoint() const {
    std::scoped_lock guard(mutex_);
    return endOfFrame_;
}

std::string SpectrogramAnimation::getName() const {
    return "spectrogram";
}

void SpectrogramAnimation::resetAnimation() {
    std::scoped_lock guard(mutex_);
    getSpectrogram()->reset();
    sendStateUnsafe(true);
}

void SpectrogramAnimation::onSpectrum(const SpectrumFrame& frame) {
    std::scoped_lock guard(mutex_);

    if (frames_.size() > MAX_BUFFER_SIZE) {
        YIO_LOG_DEBUG("spectrum buffer overflow, flushing old frames");
        frames_.clear();
    }

    frames_.push_back(std::make_pair(convertTime(frame.runningTime), frame));
}

void SpectrogramAnimation::reloadPreset() {
    std::scoped_lock guard(mutex_);
    initSpectrogram();
}

void SpectrogramAnimation::setPaused(bool paused) {
    isPaused_ = paused;
}

bool SpectrogramAnimation::isPaused() const {
    return isPaused_;
}

std::optional<std::vector<float>> SpectrogramAnimation::updateSpectrum() {
    if (frames_.empty()) {
        return std::nullopt;
    }

    while (!frames_.empty()) {
        const auto& frame = frames_.front();

        if (frame.first > endOfFrame_ + frameInterval_) {
            // too early
            return std::nullopt;
        }

        auto bands = calculateBands(frame.second);

        if (frame.first < endOfFrame_ - 2 * frameInterval_) {
            // too late
            frames_.pop_front();
            continue;
        }

        endOfFrame_ = frame.first;

        frames_.pop_front();

        return bands;
    }

    return std::nullopt;
}

std::vector<float> SpectrogramAnimation::calculateBands(const SpectrumFrame& frame) {
    std::vector<float> magnitudesLinear;
    magnitudesLinear.reserve(frame.magnitudes.size());
    for (float db : frame.magnitudes) {
        if (db <= THRESHOLD) {
            magnitudesLinear.push_back(0.0f);
        } else {
            magnitudesLinear.push_back(std::pow(10.0f, db / 10.0f));
        }
    }

    std::vector<float> bands(getSpectrogram()->getBandCount());

    const float df = (float)frame.rate / (frame.magnitudes.size() * 2 - 2);
    const float start = std::log10(getSpectrogram()->getMinFrequency() / df);
    const float stop = std::log10(getSpectrogram()->getMaxFrequency() / df);

    auto xscale = [&](size_t band) {
        float exp = start + (stop - start) * ((float)band / bands.size());

        return std::pow(10.0f, exp);
    };

    for (size_t band = 0; band < bands.size(); band++) {
        float x1 = xscale(band);
        float x2 = xscale(band + 1);

        size_t a = std::ceil(x1);
        size_t b = std::floor(x2);

        float value = 0.0f;

        if (b < a) {
            value += magnitudesLinear[b] * (x2 - x1);
        } else {
            if (a > 0) {
                value += magnitudesLinear[a - 1] * (a - x1);
            }

            while (a < b) {
                value += magnitudesLinear[a++];
            }

            if (b < bands.size()) {
                value += magnitudesLinear[b] * (x2 - b);
            }
        }

        bands[band] = value;
    }

    return bands;
}

void SpectrogramAnimation::drawSpectrogramCanvas(std::vector<float>&& bands) {
    getSpectrogram()->update(std::move(bands));

    clearSpectrogramScreen();

    if (isPaused_) {
        return;
    }

    fillSpectrogramScreen();
}

void SpectrogramAnimation::updateExternalPresets(const std::string& configs, const std::string& current, const std::string& extraData) {
    std::scoped_lock guard(mutex_);
    std::string_view oldConfigs = (externalPresets_ ? externalPresets_->configs.c_str() : "");
    std::string_view oldCurrent = (externalPresets_ ? externalPresets_->current.c_str() : "");
    std::string_view oldExtraData = (externalPresets_ ? externalPresets_->extraData.c_str() : "");
    if (oldConfigs == configs && oldCurrent == current && oldExtraData == extraData) {
        return;
    }
    if (configs.empty() || current.empty()) {
        if (!externalPresets_) {
            return;
        }
        externalPresets_.reset();
    } else {
        if (!externalPresets_) {
            externalPresets_.emplace(Presets{configs, current, extraData});
        } else {
            if (externalPresets_->configs != configs) {
                externalPresets_->configs = configs;
            }
            if (externalPresets_->current != current) {
                externalPresets_->current = current;
            }
            if (externalPresets_->extraData != extraData) {
                externalPresets_->extraData = extraData;
            }
        }
    }
    YIO_LOG_INFO("updateExternalPresets current=" << current << ", configs=" << configs);
    initSpectrogram();
}

void SpectrogramAnimation::updateLocalPresets(std::string configs) {
    std::scoped_lock guard(mutex_);
    const char* oldConfigs = (localPresets_ ? localPresets_->configs.c_str() : "");
    if (!localPresets_ || oldConfigs != configs) {
        if (!localPresets_) {
            localPresets_.emplace(Presets{std::move(configs), "random", ""});
        } else {
            localPresets_->configs = std::move(configs);
        }
        YIO_LOG_INFO("Local presets updated: " << localPresets_->configs);
        initSpectrogram();
    }
}

void SpectrogramAnimation::switchLocalCurrentPreset(std::string current) {
    std::scoped_lock guard(mutex_);
    const char* oldCurrent = (localPresets_ ? localPresets_->current.c_str() : "");
    if (current != oldCurrent) {
        if (!localPresets_) {
            localPresets_.emplace(Presets{"{}", std::move(current), ""});
        } else {
            localPresets_->current = std::move(current);
        }
        YIO_LOG_INFO("Current preset switched to " << localPresets_->current);
        initSpectrogram();
    }
}

void SpectrogramAnimation::initSpectrogram() {
    if (!localPresets_ && !externalPresets_) {
        return;
    }

    const Presets& presets = (externalPresets_ ? *externalPresets_ : *localPresets_);
    const Json::Value json = quasar::tryParseJsonOrEmpty(presets.configs);
    if (!json.isMember(presets.current)) {
        YIO_LOG_WARN("initSpectrogram cannot find preset " << presets.current);
    }

    updateSpectrogram(json, presets.current, presets.extraData);
    sendStateUnsafe(true);
}

void SpectrogramAnimation::updateTime(Animation::TimePoint timePoint) {
    bool shouldUpdate = false;
    while (timePoint >= endOfFrame_) {
        endOfFrame_ = endOfFrame_ + frameInterval_;
        shouldUpdate = true;
    }
    if (shouldUpdate) {
        updateAndDrawCanvas();
    }
}

void SpectrogramAnimation::startAnimationFrom(Animation::TimePoint timePoint) {
    endOfFrame_ = timePoint + frameInterval_;
}

std::chrono::nanoseconds SpectrogramAnimation::getLength() const {
    return frameInterval_;
}

void SpectrogramAnimation::sendStateUnsafe(bool force)
{
    if (server_ && (localPresets_ || externalPresets_)) {
        const auto& presets = (externalPresets_ ? *externalPresets_ : *localPresets_);
        const auto source = (externalPresets_ ? quasar::proto::SpectrogramAnimation::State::EXTERNAL : quasar::proto::SpectrogramAnimation::State::LOCAL);
        std::string extraData;
        if (auto spectrogram = getSpectrogram()) {
            extraData = spectrogram->getExtraData();
        }

        bool isSame =
            lastStateMessage_ &&
            lastStateMessage_->spectrogram_animation_message().state().source() == source &&
            lastStateMessage_->spectrogram_animation_message().state().configs() == presets.configs &&
            lastStateMessage_->spectrogram_animation_message().state().current() == presets.current &&
            lastStateMessage_->spectrogram_animation_message().state().extra_data() == extraData;

        if (force || !lastStateMessage_ || !isSame) {
            if (!isSame) {
                lastStateMessage_ = quasar::ipc::buildMessage([&](auto& msg) {
                    msg.mutable_spectrogram_animation_message()->mutable_state()->set_source(source);
                    msg.mutable_spectrogram_animation_message()->mutable_state()->set_configs(TString(presets.configs));
                    msg.mutable_spectrogram_animation_message()->mutable_state()->set_current(TString(presets.current));
                    msg.mutable_spectrogram_animation_message()->mutable_state()->set_extra_data(std::move(extraData));
                });
            }
            server_->sendToAll(lastStateMessage_);
        }
    }
}
