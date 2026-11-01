#include <yandex_io/libs/setup_parser/sound_utils.h>

#include <cstdlib>

#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <set>
#include <vector>

#include <boost/program_options.hpp>

using namespace quasar;
using namespace quasar::SoundUtils;

namespace bpo = boost::program_options;

bool exists(const std::string& name) {
    std::ifstream f(name.c_str());
    return f.good();
}

int main(int argc, char* argv[])
{
    std::string tokenCode;
    std::string codeType{"system"};
    int version{0};

    std::string filename;

    const std::set<int> validVersions{0, 1};
    const std::map<int, std::set<std::string>> validCodeType{
        {0, {"system", "xcode"}},
        {1, {"authcode"}},
    };

    bpo::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "produce help message")
        ("tokenCode", bpo::value<std::string>(&tokenCode)->required(), "Code to turn into sound")
        ("version", bpo::value<int>(&version)->default_value(version), "Sound protocol version. Valid values: 0, 1")
        ("filename", bpo::value<std::string>(&filename)->default_value("out.raw"), "File to save generated sound")
    ;
    // clang-format on

    bpo::variables_map opts;
    bpo::store(bpo::parse_command_line(argc, argv, desc), opts);

    try {
        bpo::notify(opts);
    } catch (const bpo::required_option& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        std::cerr << desc << std::endl;
        return 1;
    }

    if (opts.contains("help")) {
        std::cout << desc << std::endl;
        return 0;
    }

    if (!validVersions.contains(version)) {
        std::cerr << "Invalid protocol version: " << version << std::endl;
        std::cerr << desc << std::endl;
        return 1;
    }

    std::vector<unsigned char> messageBuf;

    // System is special - we need only payload
    if (codeType != "system") {
        messageBuf.push_back(4); // WifiType::WIFI_TYPE_NONE
    }

    messageBuf.push_back(static_cast<unsigned char>(tokenCode.size()));
    messageBuf.insert(messageBuf.end(), tokenCode.begin(), tokenCode.end());

    std::vector<std::vector<std::int16_t>> samples = soundDataSourceGenerateSamples(version, messageBuf, DEFAULT_SAMPLE_RATE);
    std::vector<unsigned char> byteBuf;
    for (const std::vector<std::int16_t>& sample : samples) {
        std::vector<unsigned char> toAdd = convert(sample);
        byteBuf.insert(byteBuf.end(), toAdd.begin(), toAdd.end());
    }

    std::vector<unsigned char> zeros(20000, 0);

    std::ofstream fout("out.out", std::ios::binary);

    fout.write((char*)zeros.data(), zeros.size());
    fout.write((char*)byteBuf.data(), byteBuf.size());
    fout.close();

    if (filename.find(".raw") != std::string::npos) {
        if (system(("mv out.out " + filename).c_str())) {
            std::cout << "Failed to rename out.out to " << filename << std::endl;
        }
    } else {
        if (system(("sox -t raw -r 16000 -b 16 -c 1 -L -e signed-integer out.out " + filename).c_str())) {
            std::cout << "Failed to convert from raw to " << filename << std::endl;
        }
        if (system("rm out.out")) {
            std::cout << "Failed to remove out.out" << std::endl;
        }
    }
}
