#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/draw_utils.h>
#include <smart_devices/platforms/yandexstation_2/platformd/leds/utils/gif_utils.h>

#include <library/cpp/getopt/last_getopt.h>
#include <library/cpp/resource/resource.h>

#include <util/string/vector.h>

static const quasar::Shape SCREEN_SHAPE{25, 16};

quasar::Font lookupFont(const std::string& key) {
    TString data;

    if (NResource::FindExact(key, &data)) {
        return quasar::loadFontFromString(data);
    }

    throw std::runtime_error("Unknown font " + key);
}

int main(int argc, char** argv) {
    TString input;
    TString output;
    TString fontKey;
    TString fontPath;
    bool animated;

    quasar::ReplaceList replace;

    {
        NLastGetopt::TOpts opts;

        const auto availableFonts = NResource::ListAllKeys();
        TString red;
        TString green;
        TString blue;
        TString yellow;
        TString fuchsia;
        TString aqua;

        opts.AddHelpOption('h');
        opts.AddLongOption('i', "input", "template gif path")
            .Required()
            .StoreResult(&input);
        opts.AddLongOption('o', "output", "output gif path")
            .Optional()
            .StoreResult(&output)
            .DefaultValue("output.gif");
        opts.AddLongOption('f', "font", "available fonts: " + JoinStrings(availableFonts.begin(), availableFonts.end(), ", "))
            .Optional()
            .StoreResult(&fontKey)
            .DefaultValue(NResource::KeyByIndex(0));
        opts.AddLongOption('d', "animated-font", "animated font directory")
            .Optional()
            .StoreResult(&fontPath)
            .DefaultValue(NResource::KeyByIndex(0));
        opts.AddLongOption('r', "red", "symbol for red placeholder {255, 0, 0}")
            .Optional()
            .StoreResult(&red);
        opts.AddLongOption('g', "green", "symbol for green placeholder {0, 255, 0}")
            .Optional()
            .StoreResult(&green);
        opts.AddLongOption('b', "blue", "symbol for blue placeholder {0, 0, 255}")
            .Optional()
            .StoreResult(&blue);
        opts.AddLongOption("yellow", "symbol for yellow placeholder {255, 255, 0}")
            .Optional()
            .StoreResult(&yellow);
        opts.AddLongOption("fuchsia", "symbol for fuchsia placeholder {255, 0, 255}")
            .Optional()
            .StoreResult(&fuchsia);
        opts.AddLongOption("aqua", "symbol for aqua placeholder {0, 255, 255}")
            .Optional()
            .StoreResult(&aqua);

        NLastGetopt::TOptsParseResult res(&opts, argc, argv);

        if (res.Has("font") && res.Has("animated-font")) {
            throw std::runtime_error("There should be only one font");
        }
        animated = res.Has("animated-font");
        if (res.Has("red")) {
            auto stdRed = std::string(red.data(), red.size());
            replace.push_back(quasar::SymbolToColor(stdRed, BGR24{0, 0, 255}));
        }
        if (res.Has("green")) {
            auto stdGreen = std::string(green.data(), green.size());
            replace.push_back(quasar::SymbolToColor(stdGreen, BGR24{0, 255, 0}));
        }
        if (res.Has("blue")) {
            auto stdBlue = std::string(blue.data(), blue.size());
            replace.push_back(quasar::SymbolToColor(stdBlue, BGR24{255, 0, 0}));
        }
        if (res.Has("yellow")) {
            auto stdYellow = std::string(yellow.data(), yellow.size());
            replace.push_back(quasar::SymbolToColor(stdYellow, BGR24{0, 255, 255}));
        }
        if (res.Has("fuchsia")) {
            auto stdFuchsia = std::string(fuchsia.data(), fuchsia.size());
            replace.push_back(quasar::SymbolToColor(stdFuchsia, BGR24{255, 0, 255}));
        }
        if (res.Has("aqua")) {
            auto stdAqua = std::string(aqua.data(), aqua.size());
            replace.push_back(quasar::SymbolToColor(stdAqua, BGR24{255, 255, 0}));
        }
    }

    const auto filename = std::string(input.data(), input.size());
    quasar::Gif res;
    if (animated) {
        const auto font = quasar::loadAnimatedFont(fontPath);
        res = renderFont(replace, font, quasar::loadGif(filename), SCREEN_SHAPE, 0);
    } else {
        const auto font = lookupFont(fontKey);
        res = renderFont(replace, font, filename, SCREEN_SHAPE);
    }

    quasar::saveGif(res, output);

    return 0;
}
