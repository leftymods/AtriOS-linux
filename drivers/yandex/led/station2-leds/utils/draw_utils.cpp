#include "draw_utils.h"
#include "gif_utils.h"

#include <string>

namespace {

    uint8_t mean(uint64_t sum, uint64_t count) {
        if (const uint64_t m = sum / count; m < 255) {
            return m;
        }

        return 255;
    }

    quasar::Font loadFontInternal(const quasar::Gif& gif) {
        // TODO: mapping between symbol and gif frame is now hardcoded
        static const std::string SYMBOL_TO_GIF_FRAME = ":0123456789";

        if (gif.size() != SYMBOL_TO_GIF_FRAME.size()) {
            throw std::runtime_error("Unexpected font format");
        }

        quasar::Font font;

        for (size_t i = 0; i < gif.size(); ++i) {
            font[SYMBOL_TO_GIF_FRAME[i]] = gif[i];
        }

        return font;
    }

} // namespace

namespace quasar {

    BGR24& Box::elem(int x, int y) {
        return data[y * width + x];
    }

    const BGR24& Box::elem(int x, int y) const {
        return data[y * width + x];
    }

    void draw(const Symbol& symbol, const Point& pos, Screen& screen) {
        for (int y = std::max(pos.y, 0); y < pos.y + symbol.height && y < screen.height; ++y) {
            for (int x = std::max(pos.x, 0); x < pos.x + symbol.width && x < screen.width; ++x) {
                screen.elem(x, y) = symbol.elem(x - pos.x, y - pos.y);
            }
        }
    }

    void draw(const std::string& text, const Font& font, Point pos, Screen& screen) {
        for (const char c : text) {
            const auto it = font.find(c);

            if (it == font.end()) {
                throw std::runtime_error("Don't know how to draw symbol '" + std::string(1, c) + "'");
            }

            const Symbol& symbol = it->second;

            draw(symbol, pos, screen);

            pos.x += symbol.width + 1;
        }
    }

    void putPixel(int x, int y, BGR24 color, Screen& screen) {
        if (x < 0 || screen.width <= x ||
            y < 0 || screen.height <= y)
        {
            return;
        }

        screen.elem(x, y) = color;
    }

    Font loadFont(const std::string& path) {
        return loadFontInternal(loadGif(path));
    }

    Font loadFontFromString(const std::string& data) {
        return loadFontInternal(loadGifFromString(data));
    }

    std::optional<Point> findShape(const Screen& screen, const Shape& shape, Filter match) {
        if (shape.width <= 0 || shape.height <= 0) {
            return std::nullopt;
        }

        for (int y = 0;;) {
            for (int x = 0;;) {
                if (match(screen.elem(x, y))) {
                    return findShape(screen, shape, match, {x, y});
                }

                if (x + 1 == screen.width) {
                    break;
                }

                if (int s = x + shape.width; s < screen.width) {
                    x = s;

                } else {
                    x = screen.width - 1;
                }
            }

            if (y + 1 == screen.height) {
                break;
            }

            if (int s = y + shape.height; s < screen.height) {
                y = s;

            } else {
                y = screen.height - 1;
            }
        }

        return std::nullopt;
    }

    Point findShape(const Screen& screen, const Shape& shape, Filter match, const Point& matchedPoint) {
        Point res(matchedPoint);

        while (res.y > 0 && match(screen.elem(matchedPoint.x, res.y - 1))) {
            --res.y;
        }

        if (res.y == 0) {
            res.y = matchedPoint.y;

            while (res.y + 1 < screen.height && match(screen.elem(matchedPoint.x, res.y + 1))) {
                ++res.y;
            }

            res.y -= shape.height - 1;
        }

        while (res.x > 0 && match(screen.elem(res.x - 1, matchedPoint.y))) {
            --res.x;
        }

        if (res.x == 0) {
            res.x = matchedPoint.x;

            while (res.x + 1 < screen.width && match(screen.elem(res.x + 1, matchedPoint.y))) {
                ++res.x;
            }

            res.x -= shape.width - 1;
        }

        return res;
    }

    Box scaleUp(const Box& b, uint64_t n) {
        Box res;

        res.height = b.height * n;
        res.width = b.width * n;
        res.data.resize(res.height * res.width);

        for (int y = 0; y < b.height; ++y) {
            for (int x = 0; x < b.width; ++x) {
                for (uint64_t yy = 0; yy < n; ++yy) {
                    for (uint64_t xx = 0; xx < n; ++xx) {
                        res.elem(x * n + xx, y * n + yy) = b.elem(x, y);
                    }
                }
            }
        }

        return res;
    }

    Box scaleDown(const Box& box, int n) {
        if (box.height % n != 0 || box.width % n != 0) {
            throw std::runtime_error{"Can't rescale: bad size"};
        }

        Box res;

        res.height = box.height / n;
        res.width = box.width / n;
        res.data.resize(res.height * res.width);

        for (int y = 0; y < res.height; ++y) {
            for (int x = 0; x < res.width; ++x) {
                uint64_t r = 0;
                uint64_t g = 0;
                uint64_t b = 0;

                for (int yy = 0; yy < n; ++yy) {
                    for (int xx = 0; xx < n; ++xx) {
                        const auto& pix = box.elem(x * n + xx, y * n + yy);
                        r += pix.r;
                        g += pix.g;
                        b += pix.b;
                    }
                }

                res.elem(x, y) = {mean(b, n * n), mean(g, n * n), mean(r, n * n)};
            }
        }

        return res;
    }

    bool Point::operator==(const Point& rhs) const {
        return x == rhs.x &&
               y == rhs.y;
    }

    bool Point::operator!=(const Point& rhs) const {
        return !(rhs == *this);
    }

} // namespace quasar
