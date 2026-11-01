#include "stream_src.h"

#include <sstream>
namespace quasar {

    std::string StreamSrc::getMediaTypeString() const {
        std::stringstream ss;
        ss << "audio/x-raw,format=" << format_ << ",channels=" << channels_ << ",rate=" << sampleRate_;
        return ss.str();
    }

    void StreamSrc::setSampleRate(int sampleRate) {
        sampleRate_ = sampleRate;
    }

    void StreamSrc::setUseVolumeElementStub(bool useStub) {
        useVolumeElementStub_ = useStub;
    }

    bool StreamSrc::useVolumeElementStub() const {
        return useVolumeElementStub_;
    }

    int StreamSrc::getSampleSize() const {
        return sampleSize_; // fixed S16_LE
    }
    int StreamSrc::getSampleRate() const {
        return sampleRate_;
    }
    int StreamSrc::getChannelsCount() const {
        return channels_;
    }

} // namespace quasar
