#include <smart_devices/libs/telink/swire/b91_flasher.h>
#include <smart_devices/libs/telink/swire/uart_swire.h>

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/logging/setup/setup.h>

#include <library/cpp/getopt/small/last_getopt.h>
#include <library/cpp/getopt/small/modchooser.h>

#include <util/string/cast.h>

using namespace telink;

int flashRead(int argc, const char* argv[]) {
    std::string port;
    uint32_t address;
    uint32_t size;
    TString outputPath;
    bool verbose;

    {
        NLastGetopt::TOpts opts;
        opts.AddHelpOption('h');
        opts.AddLongOption('p', "port", "TTY port")
            .Required()
            .RequiredArgument("PORT")
            .StoreResult(&port);
        opts.AddLongOption('a', "address", "read address")
            .Required()
            .RequiredArgument("ADDR")
            .StoreResult(&address);
        opts.AddLongOption('s', "size", "read size")
            .Required()
            .RequiredArgument("SIZE")
            .StoreResult(&size);
        opts.AddLongOption('o', "output", "output file")
            .Required()
            .RequiredArgument("PATH")
            .StoreResult(&outputPath);
        opts.AddLongOption('v', "verbose", "enable verbose output")
            .Optional()
            .NoArgument()
            .SetFlag(&verbose);

        opts.SetFreeArgsNum(0);

        NLastGetopt::TOptsParseResult result(&opts, argc, argv);
    }

    quasar::Logging::initLoggingToStdout(verbose ? "debug" : "info", "%+");

    B91Flasher flasher(
        std::make_shared<UartSwire>(port));

    std::vector<uint8_t> data(size);

    flasher.dump(address, data);

    TFile output{outputPath, CreateAlways | WrOnly};
    output.Write(data.data(), data.size());

    return 0;
}

int flashWrite(int argc, const char* argv[]) {
    std::string port;
    uint32_t address;
    TString inputPath;
    bool verbose;

    {
        NLastGetopt::TOpts opts;
        opts.AddHelpOption('h');
        opts.AddLongOption('p', "port", "TTY port")
            .Required()
            .RequiredArgument("PORT")
            .StoreResult(&port);
        opts.AddLongOption('a', "address", "read address")
            .Required()
            .RequiredArgument("ADDR")
            .StoreResult(&address);
        opts.AddLongOption('i', "input", "input file")
            .Required()
            .RequiredArgument("PATH")
            .StoreResult(&inputPath);
        opts.AddLongOption('v', "verbose", "enable verbose output")
            .Optional()
            .NoArgument()
            .SetFlag(&verbose);

        opts.SetFreeArgsNum(0);

        NLastGetopt::TOptsParseResult result(&opts, argc, argv);
    }

    quasar::Logging::initLoggingToStdout(verbose ? "debug" : "info", "%+");

    B91Flasher flasher(
        std::make_shared<UartSwire>(port));

    TFile input{inputPath, RdOnly};

    std::vector<uint8_t> data(input.GetLength());

    input.Load(data.data(), data.size());

    flasher.flash(address, data);

    return 0;
}

int flash(int argc, const char* argv[]) {
    TModChooser modes;
    modes.SetDescription("Telink Swire flasher");
    modes.AddMode("read", flashRead, "read flash");
    modes.AddMode("write", flashWrite, "write flash");

    return modes.Run(argc, argv);
}

int main(int argc, const char* argv[]) {
    TModChooser modes;
    modes.SetDescription("Telink Swire utility");
    modes.AddMode("flash", flash, "flash operations");

    return modes.Run(argc, argv);
}
