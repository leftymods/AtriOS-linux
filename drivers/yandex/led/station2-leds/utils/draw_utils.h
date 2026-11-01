#pragma once

#include "bgr24.h"

#include <unordered_map>

namespace quasar {

    struct Point {
        int x;
        int y;

        bool operator==(const Point& rhs) const;

        bool operator!=(const Point& rhs) const;
    };

    struct Shape {
        int width;
        int height;
    };

    struct Box: public Shape {
        Frame data;

        BGR24& elem(int x, int y);

        const BGR24& elem(int x, int y) const;
    };

    using Symbol = Box;
    using Screen = Box;

    // TODO: allow any unicode symbol
    using Font = std::unordered_map<char, Symbol>;

    Font loadFont(const std::string& path);

    Font loadFontFromString(const std::string& data);

    void draw(const Symbol& symbol, const Point& pos, Screen& screen);

    void draw(const std::string& text, const Font& font, Point pos, Screen& screen);

    using Filter = std::function<bool(const BGR24&)>;

    std::optional<Point> findShape(const Screen& screen, const Shape& shape, Filter match);

    Point findShape(const Screen& screen, const Shape& shape, Filter match, const Point& matchedPoint);

    Box scaleUp(const quasar::Box& b, uint64_t n);

    Box scaleDown(const Box& b, int n);

    void putPixel(int x, int y, BGR24 color, Screen& screen);

} // namespace quasar
