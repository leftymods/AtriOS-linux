#include "gif_utils.h"

#include <contrib/libs/giflib/gif_lib.h>

#include <yandex_io/libs/errno/errno_exception.h>
#include <yandex_io/libs/filesystem/directory_iterator.h>

#include <util/generic/scope.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <sstream>

#include <fcntl.h>
#include <unistd.h>

namespace {

    const int DEFAULT_BACKGROUND_COLOR = 0;
    const int DEFAULT_COLOR_RESOLUTION = 256;

    ColorMapObject* createDefaultCmap() {
        auto* cmap = GifMakeMapObject(DEFAULT_COLOR_RESOLUTION, nullptr);

        if (cmap == nullptr) {
            return cmap;
        }

        for (int i = 0; i < cmap->ColorCount; ++i) {
            cmap->Colors[i].Red = i;
            cmap->Colors[i].Green = i;
            cmap->Colors[i].Blue = i;
        }

        return cmap;
    }

    quasar::Gif loadGifInternal(GifFileType* gif) {
        if (DGifSlurp(gif) != GIF_OK) {
            throw std::runtime_error{"failed to read GIF data"};
        }
        if (gif->SavedImages == nullptr) {
            throw std::runtime_error{"no images found in GIF"};
        }
        quasar::Gif result;
        for (int nimage = 0; nimage < gif->ImageCount; nimage++) {
            SavedImage si = gif->SavedImages[nimage];
            int w = si.ImageDesc.Width;
            int h = si.ImageDesc.Height;
            if (w <= 0 || h <= 0) {
                std::ostringstream stringStream;
                stringStream << "invalid image dimensions " << w << " " << h;
                throw std::runtime_error{stringStream.str()};
            }
            Frame image(w * h);
            if (si.RasterBits == nullptr) {
                throw std::runtime_error{"no raster data in GIF"};
            }
            ColorMapObject* gif_cmap;
            if (si.ImageDesc.ColorMap) {
                gif_cmap = si.ImageDesc.ColorMap;
            } else if (gif->SColorMap) {
                gif_cmap = gif->SColorMap;
            } else {
                throw std::runtime_error{"color map is missing"};
            }
            for (int i = 0; i < h; i++) {
                for (int j = 0; j < w; j++) {
                    auto color = si.RasterBits[i * w + j];
                    image[i * w + j] = {gif_cmap->Colors[color].Blue, gif_cmap->Colors[color].Green,
                                        gif_cmap->Colors[color].Red};
                }
            }
            GraphicsControlBlock gcb;
            int delayTime;
            if (DGifSavedExtensionToGCB(gif, nimage, &gcb) == GIF_OK) {
                delayTime = gcb.DelayTime; // > 0 ? gcb.DelayTime : GIF_DEFAULT_DELAY_TIME;
            } else {
                throw std::runtime_error{"control block is missing"};
            }
            result.push_back({{{w, h}, std::move(image)}, delayTime});
        }
        return result;
    }

    int inputFunc(GifFileType* gif, GifByteType* dest, int size) noexcept {
        auto* stream = static_cast<std::istringstream*>(gif->UserData);

        if (stream->read((char*)dest, size)) {
            return size;
        }

        return stream->gcount();
    }

} // namespace

namespace quasar {

    Gif loadGif(const std::string& gifFilename) {
        int gifFile = open(gifFilename.c_str(), O_RDONLY);
        if (gifFile == -1) {
            throw quasar::ErrnoException(errno, "cannot open gif file '" + gifFilename + "': ");
        }
        GifFileType* gif;
        if ((gif = DGifOpenFileHandle(gifFile, nullptr)) == nullptr) {
            throw std::runtime_error{"invalid file or file not found"};
        }
        int errorCode;
        Y_DEFER {
            DGifCloseFile(gif, &errorCode);
        };

        return loadGifInternal(gif);
    }

    Gif loadGifFromString(const std::string& data) {
        GifFileType* gif;

        std::istringstream stream(data);

        if ((gif = DGifOpen(&stream, inputFunc, nullptr)) == nullptr) {
            throw std::runtime_error("read gif error");
        }

        int errorCode;
        Y_DEFER {
            DGifCloseFile(gif, &errorCode);
        };

        return loadGifInternal(gif);
    }

    void saveGif(const Gif& gif, const std::string& gifFilename) {
        if (gif.empty()) {
            throw std::runtime_error("Can't save gif: gif is empty");
        }

        GifFileType* gifFile = EGifOpenFileName(gifFilename.c_str(), false, nullptr);

        if (gifFile == nullptr) {
            throw std::runtime_error("Can't open " + gifFilename);
        }

        auto* cmap = createDefaultCmap();

        if (cmap == nullptr) {
            EGifCloseFile(gifFile, nullptr);
            throw std::runtime_error("Can't create cmap");
        }

        if (EGifPutScreenDesc(gifFile, gif.front().width, gif.front().height, DEFAULT_COLOR_RESOLUTION, DEFAULT_BACKGROUND_COLOR, cmap) == GIF_ERROR) {
            EGifCloseFile(gifFile, nullptr);
            throw std::runtime_error("Can't save screen desc");
        }

        GifFreeMapObject(cmap);

        for (size_t i = 0; i < gif.size(); ++i) {
            const auto& frame = gif[i];

            auto* image = GifMakeSavedImage(gifFile, nullptr);
            if (image == nullptr) {
                GifFreeSavedImages(gifFile);
                EGifCloseFile(gifFile, nullptr);
                throw std::runtime_error("Can't add image");
            }
            image->ImageDesc.Width = frame.width;
            image->ImageDesc.Height = frame.height;
            image->ImageDesc.Left = 0;
            image->ImageDesc.Top = 0;
            image->RasterBits = (GifByteType*)malloc(frame.width * frame.height * sizeof(GifByteType));
            if (image->RasterBits == nullptr) {
                GifFreeSavedImages(gifFile);
                EGifCloseFile(gifFile, nullptr); // frees cmap too
                throw std::runtime_error("Can't allocete raster bits");
            }
            for (size_t i = 0; i < frame.data.size(); ++i) {
                const auto& p = frame.data[i];
                image->RasterBits[i] = static_cast<GifByteType>((p.r + p.g + p.b) / 3);
            }

            GraphicsControlBlock gcb;
            gcb.DisposalMode = DISPOSE_DO_NOT;
            gcb.UserInputFlag = false;
            gcb.DelayTime = frame.durationCentiSeconds;
            gcb.TransparentColor = NO_TRANSPARENT_COLOR;

            GifByteType extensionBuffer[4];

            size_t size = EGifGCBToExtension(&gcb, extensionBuffer);

            if (GifAddExtensionBlock(&image->ExtensionBlockCount, &image->ExtensionBlocks, GRAPHICS_EXT_FUNC_CODE, size, extensionBuffer) == GIF_ERROR) {
                GifFreeSavedImages(gifFile);
                EGifCloseFile(gifFile, nullptr);
                throw std::runtime_error("Can't add gcb");
            }

            const auto& extension = image->ExtensionBlocks[0];

            if (EGifPutExtension(gifFile, extension.Function, extension.ByteCount, extension.Bytes) == GIF_ERROR) {
                GifFreeSavedImages(gifFile);
                EGifCloseFile(gifFile, nullptr);
                throw std::runtime_error("Can't write gcb");
            }

            if (EGifPutImageDesc(gifFile, 0, 0, frame.width, frame.height, false, nullptr) == GIF_ERROR) {
                GifFreeSavedImages(gifFile);
                EGifCloseFile(gifFile, nullptr);
                throw std::runtime_error("Can't write image desc");
            }

            for (int y = 0; y < frame.height; ++y) {
                if (EGifPutLine(gifFile, image->RasterBits + y * frame.width, frame.width) == GIF_ERROR) {
                    GifFreeSavedImages(gifFile);
                    EGifCloseFile(gifFile, nullptr);
                    throw std::runtime_error("Can't write image");
                }
            }
        }

        GifFreeSavedImages(gifFile);

        if (EGifCloseFile(gifFile, nullptr) == GIF_ERROR) {
            throw std::runtime_error("Can't close gif file");
        }
    }

    int getScaleFactor(const quasar::Gif& gif, const quasar::Shape& screenShape) {
        if (gif.front().width % screenShape.width != 0) {
            throw std::runtime_error("Can't get scale factor");
        }

        int scale = gif.front().width / screenShape.width;

        if (gif.front().height != screenShape.height * scale) {
            throw std::runtime_error("Can't get scale factor");
        }

        return scale;
    }

    quasar::Gif renderFont(const ReplaceList& replace,
                           const std::map<std::string, quasar::Gif>& font,
                           Gif gif,
                           const quasar::Shape& screenShape,
                           int offset) {
        if (gif.empty()) {
            throw std::runtime_error("Gif is empty");
        }
        const int scale = getScaleFactor(gif, screenShape);

        int frameN = 0;
        for (auto& frame : gif) {
            for (const auto& r : replace) {
                const auto c = r.symbol;
                const auto p = r.color;

                std::vector<quasar::Box> symbol;

                if (const auto it = font.find(c); it != font.end()) {
                    symbol = quasar::scaleUp(it->second, scale);
                } else {
                    continue;
                }
                int symbolFrame = std::clamp(frameN + offset, 0, (int)symbol.size() - 1);
                auto filter = [p](const auto& pix) { return pix.r == p.r && pix.g == p.g && pix.b == p.b; };

                if (const auto pos = quasar::findShape(frame, symbol[symbolFrame], filter); pos.has_value()) {
                    quasar::draw(symbol[symbolFrame], *pos, frame);
                }
                frameN++;
            }
        }

        quasar::Gif res;
        for (size_t i = 0; i < gif.size(); ++i) {
            res.push_back({quasar::scaleDown(gif[i], scale), gif[i].durationCentiSeconds});
        }

        return res;
    }

    quasar::Gif renderFont(const ReplaceList& replace, const quasar::Font& font,
                           const std::string& filename, const quasar::Shape& screenShape) {
        auto gif = quasar::loadGif(filename);
        if (gif.empty()) {
            throw std::runtime_error("Gif is empty: " + filename);
        }

        return renderFont(replace, font, gif, screenShape);
    }

    quasar::Gif renderFont(const ReplaceList& replace, const quasar::Font& font,
                           std::vector<GifFrame> gif, const quasar::Shape& screenShape) {
        const int scale = getScaleFactor(gif, screenShape);

        for (const auto& r : replace) {
            const auto c = r.symbol[0];
            const auto p = r.color;

            quasar::Box symbol;

            if (const auto it = font.find(c); it != font.end()) {
                symbol = quasar::scaleUp(it->second, scale);

            } else {
                throw std::runtime_error("Don't know how to draw " + std::string(1, c));
            }

            for (auto& frame : gif) {
                auto filter = [p](const auto& pix) { return pix.r == p.r && pix.g == p.g && pix.b == p.b; };

                if (const auto pos = quasar::findShape(frame, symbol, filter); pos.has_value()) {
                    quasar::draw(symbol, *pos, frame);
                }
            }
        }

        quasar::Gif res;
        for (size_t i = 0; i < gif.size(); ++i) {
            res.push_back({quasar::scaleDown(gif[i], scale), gif[i].durationCentiSeconds});
        }
        return res;
    }

    std::map<std::string, quasar::Gif> loadAnimatedFont(const std::string& path) {
        std::map<std::string, quasar::Gif> font;

        for (auto file = DirectoryIterator(path); file.isValid(); file.next()) {
            if (!file.currentEntryName().starts_with(".") && file.currentEntryName().ends_with(".gif")) {
                const auto name = file.currentEntryName().substr(0, file.currentEntryName().size() - 4);
                font[name] = quasar::loadGif(path + "/" + file.currentEntryName());
            }
        }
        return font;
    }

    std::vector<quasar::Box> scaleUp(const std::vector<quasar::GifFrame>& input, uint64_t factor) {
        std::vector<quasar::Box> result;
        result.reserve(input.size());
        std::transform(input.begin(), input.end(), std::back_inserter(result), [factor](const GifFrame& b) { return scaleUp(b, factor); });
        return result;
    }

    std::array<char, 4> getTimeDigits() {
        std::time_t t;
        std::time(&t);

        struct tm timeInfo;
        if (localtime_r(&t, &timeInfo) == nullptr) {
            throw quasar::ErrnoException(errno, "Can't get localtime");
        }

        std::array<char, 4> timeString = {
            char('0' + timeInfo.tm_hour / 10),
            char('0' + timeInfo.tm_hour % 10),
            char('0' + timeInfo.tm_min / 10),
            char('0' + timeInfo.tm_min % 10)};

        return timeString;
    }

    SymbolToColor::SymbolToColor(std::string _symbol, const BGR24 _color)
        : symbol(std::move(_symbol))
        , color(_color)
    {
    }
} // namespace quasar
