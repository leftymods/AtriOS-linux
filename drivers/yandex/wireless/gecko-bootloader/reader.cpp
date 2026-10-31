#include "reader.h"

namespace {

    struct GblTagHeader {
        GblTagId id;
        uint32_t length;
    };

} // namespace

GblReader::GblReader(const std::string& filename)
    : gbl_(filename, std::ios_base::in | std::ios_base::binary)
{
}

std::optional<GblTag> GblReader::nextTag() {
    GblTagHeader header;

    if (!gbl_.read(reinterpret_cast<char*>(&header), sizeof(header))) {
        return std::nullopt;
    }

    GblTag tag;
    tag.id = header.id;
    tag.data.resize(header.length);

    if (!gbl_.read(tag.data.data(), tag.data.size())) {
        return std::nullopt;
    }

    return tag;
}

std::optional<std::string> GblReader::getMetadata(const std::string& filename) {
    GblReader reader(filename);

    auto tag = reader.nextTag();
    if (!tag || tag->id != GblTagId::HEADER) {
        return std::nullopt;
    }

    while (true) {
        tag = reader.nextTag();
        if (!tag) {
            return std::nullopt;
        }

        if (tag->id == GblTagId::METADATA) {
            return tag->data;
        } else if (tag->id == GblTagId::END) {
            return std::nullopt;
        }
    }
}
