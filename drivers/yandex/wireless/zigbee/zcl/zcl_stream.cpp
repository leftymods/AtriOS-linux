#include "zcl_stream.h"

#include <sstream>

namespace zigbee {

    ZclStream::ZclStream(const std::vector<uint8_t>& data)
        : data_(data)
    {
    }

    bool ZclStream::isEmpty() const {
        return position_ >= data_.size();
    }

    std::vector<uint8_t> ZclStream::getRemainingBuffer() const {
        if (position_ >= data_.size()) {
            throw std::invalid_argument("stream is empty");
        } else {
            return std::vector<uint8_t>{data_.begin() + position_, data_.end()};
        }
    }

    uint8_t ZclStream::readInt8()
    {
        verifyByteCount(1);
        return data_[position_++];
    }

    uint8_t ZclStream::readUint8()
    {
        verifyByteCount(1);
        return data_[position_++];
    }

    int16_t ZclStream::readInt16()
    {
        int16_t value;
        verifyByteCount(sizeof(value));

        value = data_[position_] + (data_[position_ + 1] << 8);
        position_ += sizeof(value);

        return value;
    }

    uint16_t ZclStream::readUint16()
    {
        uint16_t value;
        verifyByteCount(sizeof(value));

        value = data_[position_] + (data_[position_ + 1] << 8);
        position_ += sizeof(value);

        return value;
    }

    void ZclStream::verifyByteCount(size_t byteCount) const {
        if (position_ + byteCount > data_.size()) {
            size_t remainingBytes = data_.size() - position_;

            std::stringstream ss;
            ss << "Failed to read " << byteCount << " bytes: " << remainingBytes << " bytes left";

            throw std::invalid_argument(ss.str());
        }
    }

    ZclOutputStream::ZclOutputStream(std::vector<uint8_t>& buffer)
        : buffer_(buffer)
    {
    }

    void ZclOutputStream::putByte(uint8_t value) {
        buffer_.push_back(value);
    }

} // namespace zigbee
