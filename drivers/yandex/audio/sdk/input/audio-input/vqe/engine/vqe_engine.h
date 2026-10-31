#pragma once

#include <yandex_io/libs/device/defines.h>
#include <yandex_io/sdk/audio_source/defines.h>
#include <yandex_io/sdk/audio_source/feedback_source.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include <span>

namespace YandexIO {

    class VQEEngine {
    public:
        struct ChannelCount {
            size_t micCount;
            size_t spkCount;
        };

    public:
        virtual ~VQEEngine();

        virtual void process(const std::vector<float>& inputMic,
                             const std::vector<float>& inputSpk,
                             double& doaAngle, bool& speechDetected) = 0;

        virtual ChannelCount getInputChannelCount() const = 0;
        virtual size_t getOutputChannelCount(ChannelData::Type) const = 0;
        virtual std::span<const float> getOutputChannelData(ChannelData::Type, size_t channelId) const = 0;

        virtual void setOmniMode(bool /* omniMode */) {
            throw std::runtime_error("OMNI directional mode is not supported for this type of VQE");
        }

        /**
         * Sets speaker volume, allows to adjust VQE
         * @param volume speaker volume in Alice scale (from 0 to 10)
         */
        virtual void setSpeakerVolume(int /* volume */) {
            throw std::runtime_error("setSpeakerVolume(int volume) is not supported on this type of VQE");
        }

        virtual int getPeriodSize() const {
            throw std::runtime_error("getPeriodSize() is not supported for this type of VQE");
        }

        /**
         * @return number of samples Feedback data is ahead of mic data. return std::nullopt if shift is
         *         not calculated yet
         */
        virtual std::optional<int> getFeedbackShift() const {
            throw std::runtime_error("getFeedbackShift() is not supported for this type of VQE");
        }

        virtual std::optional<float> getFeedbackShiftCorrelation() const {
            throw std::runtime_error("getFeedbackShiftCorrelation() is not supported for this type of VQE");
        }

        virtual FeedbackSource hardwareSyncTarget() const {
            throw std::runtime_error("hardwareSyncTarget() is not supported for this type of VQE");
        }
    };

} // namespace YandexIO
