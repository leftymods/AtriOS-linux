#include "types.h"
#include "zcl_stream.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <sstream>

using namespace zigbee;
using namespace zigbee::zcl;

template <>
AnyType ZclStream::read<AnyType>();

template <>
Bool ZclStream::read<Bool>() {
    return static_cast<bool>(readUint8());
}

template <>
Map8 ZclStream::read<Map8>() {
    return readUint8();
}

template <>
Map16 ZclStream::read<Map16>() {
    return readUint16();
}

template <>
Map24 ZclStream::read<Map24>() {
    uint32_t value = 0;

    value |= readUint8();
    value |= readUint8() << 8;
    value |= readUint8() << 16;

    return value;
}

template <>
Map32 ZclStream::read<Map32>() {
    uint32_t value = 0;

    value |= readUint8();
    value |= readUint8() << 8;
    value |= readUint8() << 16;
    value |= readUint8() << 24;

    return value;
}

template <>
Uint8 ZclStream::read<Uint8>() {
    return readUint8();
}

template <>
Uint16 ZclStream::read<Uint16>() {
    return readUint16();
}

template <>
Uint24 ZclStream::read<Uint24>() {
    uint32_t value = 0;

    value |= readUint8();
    value |= readUint8() << 8;
    value |= readUint8() << 16;

    return value;
}

template <>
Uint32 ZclStream::read<Uint32>() {
    uint32_t value = 0;

    value |= readUint8();
    value |= readUint8() << 8;
    value |= readUint8() << 16;
    value |= readUint8() << 24;

    return value;
}

template <>
Uint40 ZclStream::read<Uint40>() {
    uint64_t value = 0;

    value |= static_cast<uint64_t>(readUint8());
    value |= static_cast<uint64_t>(readUint8()) << 8;
    value |= static_cast<uint64_t>(readUint8()) << 16;
    value |= static_cast<uint64_t>(readUint8()) << 24;
    value |= static_cast<uint64_t>(readUint8()) << 32;

    return value;
}

template <>
Uint48 ZclStream::read<Uint48>() {
    uint64_t value = 0;

    value |= static_cast<uint64_t>(readUint8());
    value |= static_cast<uint64_t>(readUint8()) << 8;
    value |= static_cast<uint64_t>(readUint8()) << 16;
    value |= static_cast<uint64_t>(readUint8()) << 24;
    value |= static_cast<uint64_t>(readUint8()) << 32;
    value |= static_cast<uint64_t>(readUint8()) << 40;

    return value;
}

template <>
Int8 ZclStream::read<Int8>() {
    return readInt8();
}

template <>
Int16 ZclStream::read<Int16>() {
    return readInt16();
}

template <>
Int24 ZclStream::read<Int24>() {
    int32_t value = 0;

    value |= readUint8() << 8;
    value |= readUint8() << 16;
    value |= readUint8() << 24;

    return value >> 8;
}

template <>
Int32 ZclStream::read<Int32>() {
    int32_t value = 0;

    value |= readUint8();
    value |= readUint8() << 8;
    value |= readUint8() << 16;
    value |= readUint8() << 24;

    return value;
}

template <>
Enum8 ZclStream::read<Enum8>() {
    return readUint8();
}

template <>
Enum16 ZclStream::read<Enum16>() {
    return readUint16();
}

template <>
Single ZclStream::read<Single>() {
    static_assert(sizeof(float) == 4);

    float value = 0;
    uint8_t* data = reinterpret_cast<uint8_t*>(&value);

    data[0] = readUint8();
    data[1] = readUint8();
    data[2] = readUint8();
    data[3] = readUint8();

    return value;
}

template <>
Octstr ZclStream::read<Octstr>() {
    const size_t length = readUint8();
    if (length == 0xff) {
        return std::string();
    }

    std::string value;
    value.reserve(length);

    for (size_t i = 0; (i < length) && !isEmpty(); i++) {
        value.push_back(readUint8());
    }

    if (value.size() != length) {
        YIO_LOG_WARN("Octstr length is " << value.size() << ", expected " << length);
    }

    return value;
}

template <>
String ZclStream::read<String>() {
    const size_t length = readUint8();
    if (length == 0xff) {
        return std::string();
    }

    std::string value;
    value.reserve(length);

    for (size_t i = 0; (i < length) && !isEmpty(); i++) {
        value.push_back(readUint8());
    }

    if (value.size() != length) {
        YIO_LOG_WARN("String length is " << value.size() << ", expected " << length);
    }

    return value;
}

template <>
Struct ZclStream::read<Struct>() {
    const size_t length = readUint16();
    if (length == 0xffff) {
        return Struct{};
    }

    std::vector<AnyType> value;
    value.resize(length);

    for (size_t i = 0; i < length; i++) {
        value[i] = read<AnyType>();
    }

    return value;
}

template <>
AttribId ZclStream::read<AttribId>() {
    return readUint16();
}

template <>
Eui64 ZclStream::read<Eui64>() {
    std::array<uint8_t, 8> eui64;

    for (uint8_t& byte : eui64) {
        byte = readUint8();
    }

    return eui64;
}

static Unk readUnk(ZclStream& stream, ZclType type) {
    switch (type) {
        case ZclType::Bool:
            return stream.read<Bool>();
        case ZclType::Map8:
            return stream.read<Map8>();
        case ZclType::Map16:
            return stream.read<Map16>();
        case ZclType::Map24:
            return stream.read<Map24>();
        case ZclType::Map32:
            return stream.read<Map32>();
        case ZclType::Uint8:
            return stream.read<Uint8>();
        case ZclType::Uint16:
            return stream.read<Uint16>();
        case ZclType::Uint24:
            return stream.read<Uint24>();
        case ZclType::Uint32:
            return stream.read<Uint32>();
        case ZclType::Uint40:
            return stream.read<Uint40>();
        case ZclType::Uint48:
            return stream.read<Uint48>();
        case ZclType::Int8:
            return stream.read<Int8>();
        case ZclType::Int16:
            return stream.read<Int16>();
        case ZclType::Int24:
            return stream.read<Int24>();
        case ZclType::Int32:
            return stream.read<Int32>();
        case ZclType::Enum8:
            return stream.read<Enum8>();
        case ZclType::Enum16:
            return stream.read<Enum16>();
        case ZclType::Single:
            return stream.read<Single>();
        case ZclType::Octstr:
            return stream.read<Octstr>();
        case ZclType::String:
            return stream.read<String>();
        case ZclType::Struct:
            return stream.read<Struct>();
        case ZclType::Eui64:
            return stream.read<Eui64>();
        default: {
            std::ostringstream ss;
            ss << "Bad type: " << quasar::Hex(static_cast<uint8_t>(type));
            throw std::invalid_argument(ss.str());
            break;
        }
    }
}

template <>
AnyType ZclStream::read<AnyType>() {
    AnyType result;

    result.type = read<ZclType>();
    result.value = readUnk(*this, result.type);

    return result;
}

template <>
WriteStructuredRecord ZclStream::read<WriteStructuredRecord>() {
    WriteStructuredRecord result = {};

    result.attributeIdentifier = read<AttribId>();
    result.selector = readArray<Uint16, 1>();
    result.attribute = read<AnyType>();

    return result;
}

template <>
WriteAttributeRecord ZclStream::read<WriteAttributeRecord>() {
    WriteAttributeRecord result = {};

    result.attributeIdentifier = read<AttribId>();
    result.attribute = read<AnyType>();

    return result;
}

template <>
ReadAttributeResponseRecord ZclStream::read<ReadAttributeResponseRecord>() {
    ReadAttributeResponseRecord result = {};

    result.attributeIdentifier = read<AttribId>();
    result.status = read<ZclStatus>();
    if (result.status == ZclStatus::Success) {
        result.attribute = read<AnyType>();
    }

    return result;
}

template <>
AttributeReportRecord ZclStream::read<AttributeReportRecord>() {
    AttributeReportRecord result = {};

    result.attributeIdentifier = read<AttribId>();
    result.attribute = read<AnyType>();

    return result;
}

template <>
ConfigureReportingRecord ZclStream::read<ConfigureReportingRecord>() {
    ConfigureReportingRecord result = {};

    result.direction = read<ReportingRole>();
    result.attributeIdentifier = read<AttribId>();
    if (result.direction == ReportingRole::Generator) {
        result.attributeType = read<ZclType>();
        result.minimumReportingInterval = read<Uint16>();
        result.maximumReportingInterval = read<Uint16>();
        result.reportableChange = readUnk(*this, result.attributeType); // XXX discrete
    } else {
        result.timeout = read<Uint16>();
    }

    return result;
}

template <>
ConfigureReportingResponseRecord ZclStream::read<ConfigureReportingResponseRecord>() {
    ConfigureReportingResponseRecord result = {};

    result.status = read<ZclStatus>();
    if (result.status != ZclStatus::Success) {
        result.direction = read<ReportingRole>();
        result.attributeIdentifier = read<AttribId>();
    }

    return result;
}

template <>
DiscoverAttributesExtendedResponseRecord ZclStream::read<DiscoverAttributesExtendedResponseRecord>() {
    DiscoverAttributesExtendedResponseRecord result = {};

    result.attributeIdentifier = read<AttribId>();
    result.attributeType = read<ZclType>();
    result.accessControl = read<Map8>();

    return result;
}

template <>
DiscoverAttributesResponseRecord ZclStream::read<DiscoverAttributesResponseRecord>() {
    DiscoverAttributesResponseRecord result = {};

    result.attributeIdentifier = read<AttribId>();
    result.attributeType = read<ZclType>();

    return result;
}

template <>
ReadReportingConfigurationRecord ZclStream::read<ReadReportingConfigurationRecord>() {
    ReadReportingConfigurationRecord result = {};

    result.direction = read<ReportingRole>();
    result.attributeIdentifier = read<AttribId>();

    return result;
}

template <>
ReadReportingConfigurationResponseRecord ZclStream::read<ReadReportingConfigurationResponseRecord>() {
    ReadReportingConfigurationResponseRecord result = {};

    result.status = read<ZclStatus>();
    result.direction = read<ReportingRole>();
    result.attributeIdentifier = read<AttribId>();
    if (result.status == ZclStatus::Success && result.direction == ReportingRole::Generator) {
        result.attributeType = read<ZclType>();
        result.minimumReportingInterval = read<Uint16>();
        result.maximumReportingInterval = read<Uint16>();
        result.reportableChange = readUnk(*this, result.attributeType); // XXX discrete
    }
    if (result.status == ZclStatus::Success && result.direction == ReportingRole::Recipient) {
        result.timeout = read<Uint16>();
    }

    return result;
}

template <>
ReadStructuredRecord ZclStream::read<ReadStructuredRecord>() {
    ReadStructuredRecord result = {};

    result.attributeIdentifier = read<AttribId>();
    result.index = readArray<Uint16, 1>();

    return result;
}

template <>
WriteAttributeResponseRecord ZclStream::read<WriteAttributeResponseRecord>() {
    WriteAttributeResponseRecord result = {};

    result.status = read<ZclStatus>();
    if (result.status != ZclStatus::Success) {
        result.attributeIdentifier = read<AttribId>();
    }

    return result;
}

template <>
WriteStructuredResponseRecord ZclStream::read<WriteStructuredResponseRecord>() {
    WriteStructuredResponseRecord result = {};

    result.status = read<ZclStatus>();
    if (result.status != ZclStatus::Success) {
        result.attributeIdentifier = read<AttribId>();
        result.selector = readArray<Uint16, 1>();
    }

    return result;
}

template <>
void ZclOutputStream::write<Bool>(const Bool& value) {
    putByte(static_cast<uint8_t>(value));
}

template <>
void ZclOutputStream::write<Map8>(const Map8& value) {
    putByte(value);
}

template <>
void ZclOutputStream::write<Map16>(const Map16& value) {
    putByte(value & 0xff);
    putByte(value >> 8);
}

template <>
void ZclOutputStream::write<Uint8>(const Uint8& value) {
    putByte(value);
}

template <>
void ZclOutputStream::write<Uint16>(const Uint16& value) {
    putByte(value & 0xff);
    putByte(value >> 8);
}

template <>
void ZclOutputStream::write<Uint32>(const Uint32& value) {
    putByte(value & 0xff);
    putByte((value >> 8) & 0xff);
    putByte((value >> 16) & 0xff);
    putByte((value >> 24) & 0xff);
}

template <>
void ZclOutputStream::write<Int8>(const Int8& value) {
    putByte(value);
}

template <>
void ZclOutputStream::write<Int16>(const Int16& value) {
    putByte(value & 0xff);
    putByte(value >> 8);
}

template <>
void ZclOutputStream::write<Enum8>(const Enum8& value) {
    putByte(value);
}

template <>
void ZclOutputStream::write<Enum16>(const Enum16& value) {
    putByte(value & 0xff);
    putByte(value >> 8);
}

template <>
void ZclOutputStream::write<Octstr>(const Octstr& value) {
    const std::string& str = value;

    if (str.length() >= 0xff) {
        throw std::invalid_argument("Octstr is too long");
        return;
    }

    putByte(str.length());

    for (size_t i = 0; i < str.length(); i++) {
        putByte(str[i]);
    }
}

template <>
void ZclOutputStream::write<Single>(const Single& value) {
    static_assert(sizeof(float) == 4);

    const float v = value;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(&v);

    putByte(data[0]);
    putByte(data[1]);
    putByte(data[2]);
    putByte(data[3]);
}

template <>
void ZclOutputStream::write<AttribId>(const AttribId& value) {
    putByte(value & 0xff);
    putByte(value >> 8);
}

template <>
void ZclOutputStream::write<Eui64>(const Eui64& value) {
    const std::array<uint8_t, 8>& eui64 = value;

    for (uint8_t byte : eui64) {
        putByte(byte);
    }
}

static void writeUnk(ZclOutputStream& stream, ZclType type, const Unk& value) {
    switch (type) {
        case ZclType::Uint8:
            stream.write<Uint8>(std::any_cast<Uint8>(value));
            break;
        case ZclType::Uint16:
            stream.write<Uint16>(std::any_cast<Uint16>(value));
            break;
        case ZclType::Eui64:
            stream.write<Eui64>(std::any_cast<Eui64>(value));
            break;
        case ZclType::Single:
            stream.write<Single>(std::any_cast<Single>(value));
            break;
        default: {
            std::ostringstream ss;
            ss << "Bad type: " << quasar::Hex(static_cast<uint8_t>(type));
            throw std::invalid_argument(ss.str());
            break;
        }
    }
}

template <>
void ZclOutputStream::write<AnyType>(const AnyType& value) {
    write<ZclType>(value.type);
    writeUnk(*this, value.type, value.value);
}

template <>
void ZclOutputStream::write<WriteStructuredRecord>(const WriteStructuredRecord& value) {
    write<AttribId>(value.attributeIdentifier);
    writeArray<Uint16, 1>(value.selector);
    write<AnyType>(value.attribute);
}

template <>
void ZclOutputStream::write<WriteAttributeRecord>(const WriteAttributeRecord& value) {
    write<AttribId>(value.attributeIdentifier);
    write<AnyType>(value.attribute);
}

template <>
void ZclOutputStream::write<ReadAttributeResponseRecord>(const ReadAttributeResponseRecord& value) {
    write<AttribId>(value.attributeIdentifier);
    write<ZclStatus>(value.status);
    if (value.status == ZclStatus::Success) {
        write<AnyType>(value.attribute);
    }
}

template <>
void ZclOutputStream::write<AttributeReportRecord>(const AttributeReportRecord& value)
{
    write<AttribId>(value.attributeIdentifier);
    write<AnyType>(value.attribute);
}

template <>
void ZclOutputStream::write<ConfigureReportingRecord>(const ConfigureReportingRecord& value)
{
    write<ReportingRole>(value.direction);
    write<AttribId>(value.attributeIdentifier);
    if (value.direction == ReportingRole::Generator) {
        write<ZclType>(value.attributeType);
        write<Uint16>(value.minimumReportingInterval);
        write<Uint16>(value.maximumReportingInterval);
        writeUnk(*this, value.attributeType, value.reportableChange); // XXX discrete
    } else {
        write<Uint16>(value.timeout);
    }
}

template <>
void ZclOutputStream::write<ConfigureReportingResponseRecord>(const ConfigureReportingResponseRecord& value) {
    write<ZclStatus>(value.status);
    if (value.status != ZclStatus::Success) {
        write<ReportingRole>(value.direction);
        write<AttribId>(value.attributeIdentifier);
    }
}

template <>
void ZclOutputStream::write<DiscoverAttributesResponseRecord>(const DiscoverAttributesResponseRecord& value) {
    write<AttribId>(value.attributeIdentifier);
    write<ZclType>(value.attributeType);
}

template <>
void ZclOutputStream::write<DiscoverAttributesExtendedResponseRecord>(const DiscoverAttributesExtendedResponseRecord& value) {
    write<AttribId>(value.attributeIdentifier);
    write<ZclType>(value.attributeType);
    write<Map8>(value.accessControl);
}

template <>
void ZclOutputStream::write<ReadReportingConfigurationRecord>(const ReadReportingConfigurationRecord& value) {
    write<ReportingRole>(value.direction);
    write<AttribId>(value.attributeIdentifier);
}

template <>
void ZclOutputStream::write<ReadReportingConfigurationResponseRecord>(const ReadReportingConfigurationResponseRecord& value) {
    write<ZclStatus>(value.status);
    write<ReportingRole>(value.direction);
    write<AttribId>(value.attributeIdentifier);
    if (value.direction == ReportingRole::Generator) {
        write<ZclType>(value.attributeType);
        write<Uint16>(value.minimumReportingInterval);
        write<Uint16>(value.maximumReportingInterval);
        writeUnk(*this, value.attributeType, value.reportableChange); // XXX discrete
    } else {
        write<Uint16>(value.timeout);
    }
}

template <>
void ZclOutputStream::write<ReadStructuredRecord>(const ReadStructuredRecord& value) {
    write<AttribId>(value.attributeIdentifier);
    writeArray<Uint16, 1>(value.index);
}

template <>
void ZclOutputStream::write<WriteAttributeResponseRecord>(const WriteAttributeResponseRecord& value) {
    write<ZclStatus>(value.status);
    if (value.status != ZclStatus::Success) {
        write<AttribId>(value.attributeIdentifier);
    }
}

template <>
void ZclOutputStream::write<WriteStructuredResponseRecord>(const WriteStructuredResponseRecord& value) {
    write<ZclStatus>(value.status);
    if (value.status != ZclStatus::Success) {
        write<AttribId>(value.attributeIdentifier);
        writeArray<Uint16, 1>(value.selector);
    }
}
