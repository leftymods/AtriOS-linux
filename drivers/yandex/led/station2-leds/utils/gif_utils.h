#pragma once

#include "gif_frame.h"

#include <map>
#include <string>

namespace quasar {

    struct SymbolToColor {
        SymbolToColor(std::string _symbol, BGR24 _color);
        std::string symbol;
        BGR24 color;
    };

    using ReplaceList = std::vector<SymbolToColor>;

    using Gif = std::vector<GifFrame>;

    Gif loadGif(const std::string& gifFilename);

    Gif loadGifFromString(const std::string& data);

    void saveGif(const Gif& gif, const std::string& gifFilename);

    quasar::Gif renderFont(const ReplaceList& replace, const quasar::Font& font,
                           const std::string& filename, const quasar::Shape& screenShape);

    quasar::Gif renderFont(const ReplaceList& replace, const std::map<std::string, quasar::Gif>& font, Gif gif, const quasar::Shape& screenShape, int offset);

    quasar::Gif renderFont(const ReplaceList& replace, const quasar::Font& font, std::vector<GifFrame> gif, const quasar::Shape& screenShape);

    std::map<std::string, quasar::Gif> loadAnimatedFont(const std::string& path);

    std::vector<quasar::Box> scaleUp(const std::vector<quasar::GifFrame>& input, uint64_t factor);

    std::array<char, 4> getTimeDigits();

} // namespace quasar
