#pragma once

#include <vector>

namespace zigbee {

    class ZclStream {
    public:
        explicit ZclStream(const std::vector<uint8_t>& data);
        ZclStream(const ZclStream& other) = delete;
        ZclStream& operator=(const ZclStream& other) const = delete;

        bool isEmpty() const;
        std::vector<uint8_t> getRemainingBuffer() const;

        uint8_t readInt8();
        uint8_t readUint8();
        int16_t readInt16();
        uint16_t readUint16();

        template <typename T>
        T read();

        template <typename T, std::size_t LengthSize, std::enable_if_t<LengthSize == 0, int> = 0>
        std::vector<T> readArray() {
            std::vector<T> result;

            while (!isEmpty()) {
                result.push_back(read<T>());
            }

            return result;
        }

        template <typename T, std::size_t LengthSize, std::enable_if_t<LengthSize == 1, int> = 0>
        std::vector<T> readArray() {
            uint8_t length = readUint8();

            std::vector<T> result;
            result.reserve(length);

            for (int i = 0; i < length; i++) {
                result.push_back(read<T>());
            }

            return result;
        }

    private:
        void verifyByteCount(size_t byteCount) const;

    private:
        size_t position_ = 0;
        const std::vector<uint8_t>& data_;
    };

    class ZclOutputStream {
    public:
        ZclOutputStream(std::vector<uint8_t>& buffer);

        void putByte(uint8_t value);

        template <typename T>
        void write(const T& value);

        template <typename T, std::size_t LengthSize>
        void writeArray(const std::vector<T>& array) {
            if constexpr (LengthSize == 1) {
                putByte(array.size());
            } else {
                static_assert(LengthSize == 0);
            }

            for (const auto& value : array) {
                write(value);
            }
        }

    private:
        std::vector<uint8_t>& buffer_;
    };

} // namespace zigbee
