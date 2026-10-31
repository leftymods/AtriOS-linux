#include "audio_device_base.h"

#include <yandex_io/libs/audio/common/defines.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <util/system/yassert.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>

using namespace quasar;

namespace {

    std::set<std::string> jsonToStringSet(const Json::Value& jsonArray) {
        Y_VERIFY(jsonArray.isArray());
        std::set<std::string> result;
        for (const auto& jsonElem : jsonArray) {
            const auto& jsonElemStr = jsonElem.asString();
            if (jsonElemStr == "*") {
                // For optimizing all-available-channels capturing case
                return {};
            }
            result.insert(jsonElem.asString());
        }
        return result;
    }

    constexpr size_t DEFAULT_CHANNEL_BUFFER_CAPACITY_SEC = 3;

    struct StringVsPair {
        template <typename T_>
        bool operator()(const std::string& a, const T_& b) const {
            return a < b.first;
        }
        template <typename T_>
        bool operator()(const T_& a, const std::string& b) const {
            return a.first < b;
        }
    };

} // namespace

namespace YandexIO {

    AudioDeviceBase::AudioDeviceBase(ChannelsList&& availableChannelsParam,
                                     const Json::Value& config)
        : AudioDevice(std::move(availableChannelsParam))
        , channelBufferSize_(tryGetInt(config, "channelBufferCapacitySec", DEFAULT_CHANNEL_BUFFER_CAPACITY_SEC) *
                             tryGetInt(config, "inRate", DEFAULT_AUDIO_SAMPLING_RATE))
    {
        // prepare channels to capture
        const auto& channelsToCapture = jsonToStringSet(config["capturedChannels"]);
        const auto& availableChannels = getAvailableChannels();
        if (!channelsToCapture.empty()) {
            if (!std::includes(std::begin(channelsToCapture), std::end(channelsToCapture),
                               std::begin(availableChannels), std::end(availableChannels),
                               StringVsPair())) {
                throw std::runtime_error("requested to capture channels aren't available");
            }

            for (const auto& channelName : channelsToCapture) {
                channelToBufferMap_.emplace(channelName, channelBufferSize_);
            }
        } else {
            for (const auto& channel : availableChannels) {
                channelToBufferMap_.emplace(channel.first, channelBufferSize_);
            }
        }
    }

    AudioDeviceBase::~AudioDeviceBase() {
        AudioDeviceBase::stop();
    }

    void AudioDeviceBase::start() {
        bool expected = true;
        if (isStopped_.compare_exchange_strong(expected, false)) {
            workingThread_ = std::thread(&AudioDeviceBase::captureThreadProc, this);
            YIO_LOG_INFO("Started " << workingThread_.get_id() << " thread");
        } else {
            YIO_LOG_WARN("AudioDevice capture thread already started");
        }
    }

    void AudioDeviceBase::stop() {
        bool expected = false;
        if (isStopped_.compare_exchange_strong(expected, true)) {
            YIO_LOG_INFO("Joining " << workingThread_.get_id() << " thread");
            if (workingThread_.joinable()) {
                workingThread_.join();
            } else {
                YIO_LOG_WARN("AudioDevice capture thread is not joinable!");
            }
        } else {
            YIO_LOG_WARN("AudioDevice capture thread already stopped");
        }
    }

    bool AudioDeviceBase::isAllChannelsCaptured() const {
        return numberOfChannelsCaptured_ >= channelToBufferMap_.size() - getAbsentChannelCount();
    }

    AudioDevice::ChannelToChunk AudioDeviceBase::capture() {
        std::unique_lock<std::mutex> lock(dataMutex_);
        dataCV_.wait(lock, [this] { return isAllChannelsCaptured(); });

        AudioDevice::ChannelToChunk outChannelToChunk;
        const auto doaAngle = getDOAAngle();
        for (auto& [channelName, ringBuffer] : channelToBufferMap_) {
            if (ringBuffer.getSize() == 0) {
                continue;
            }

            auto [insertPoint, inserted] = outChannelToChunk.emplace(channelName, Chunk(std::size_t(ringBuffer.getSize()), doaAngle));
            if (insertPoint->second.data.size()) {
                ringBuffer.read(&insertPoint->second.data[0], insertPoint->second.data.size());
            }
            ringBuffer.clear();
        }
        numberOfChannelsCaptured_ = 0;
        if (std::exchange(wasOverflow, false)) {
            YIO_LOG_INFO("On AudioDevice Become Normal");
        }
        return outChannelToChunk;
    }

    void AudioDeviceBase::setSpotterMode() {
        // do nothing by default
    }

    void AudioDeviceBase::setASRMode() {
        // do nothing by default
    }

    void AudioDeviceBase::pushData(const ChannelToData& audioData) {
        std::lock_guard<std::mutex> lock(dataMutex_);

        for (const auto& [channelName, dataVec] : audioData) {
            auto bufIter = channelToBufferMap_.find(channelName);
            if (bufIter != channelToBufferMap_.end()) {
                auto& ringBuffer = bufIter->second;
                auto curSize = ringBuffer.getSize();
                if (ringBuffer.getSize() + dataVec.size() > ringBuffer.getCapacity()) {
                    wasOverflow = true;
                }
                ringBuffer.write(dataVec.data(), dataVec.size());
                if (curSize == 0 && ringBuffer.getSize()) {
                    ++numberOfChannelsCaptured_;
                }
            }
        }
        if (wasOverflow) {
            YIO_LOG_WARN("On AudioDevice Buffer overflow");
        }

        if (isAllChannelsCaptured()) {
            dataCV_.notify_one();
        } else if (allChannelsAtOnce_) {
            YIO_LOG_WARN("only " << numberOfChannelsCaptured_ << " channels captured!");
        }
    }

    bool AudioDeviceBase::isChannelCaptured(const std::string& channelName) const {
        return channelToBufferMap_.count(channelName);
    }

    void AudioDeviceBase::onCaptureThreadStart() {
        // no-op by default
    }

    void AudioDeviceBase::onCaptureThreadStop() {
        // no-op by default
    }

    unsigned AudioDeviceBase::getAbsentChannelCount() const {
        return 0;
    }

    void AudioDeviceBase::captureThreadProc() {
        onCaptureThreadStart();

        while (!isStopped_.load()) {
            doCapture();
        }
        onCaptureThreadStop();
    }
} // namespace YandexIO
