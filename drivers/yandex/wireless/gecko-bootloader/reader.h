#pragma once

#include <fstream>
#include <optional>

enum class GblTagId: uint32_t {
    HEADER = 0x03A617EB,
    BOOTLOADER = 0xF50909F5,
    APPLICATION = 0xF40A0AF4,
    METADATA = 0xF60808F6,
    PROG = 0xFE0101FE,
    ERASEPROG = 0xFD0303FD,
    END = 0xFC0404FC,
    SE_UPGRADE = 0x5EA617EB,
    SIGNATURE_ECDSA_P256 = 0xF70A0AF7,
    CERTIFICATE_ECDSA_P256 = 0xF30B0BF3,
};

struct GblTag {
    GblTagId id;
    std::string data;
};

class GblReader {
public:
    GblReader(const std::string& filename);

    std::optional<GblTag> nextTag();

    static std::optional<std::string> getMetadata(const std::string& filename);

private:
    std::ifstream gbl_;
};
