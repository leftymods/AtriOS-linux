#include <algorithm>
#include <fstream>
#include <locale>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>

#include <yandex_io/libs/base/utils.h>

int main(int /* argc */, char** argv)
{
    const std::string patternName = argv[1];
    std::ifstream patternFile(patternName);

    if (patternFile.is_open()) {
        std::vector<uint8_t> bytes;

        std::string line;
        while (std::getline(patternFile, line)) {
            line = quasar::trim(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }

            if (line.rfind("loop", 0) == 0) {
                bytes.emplace_back(1);
                continue;
            }

            bytes.emplace_back(0);
            std::istringstream stream(line);
            auto values = std::vector<uint16_t>(std::istream_iterator<uint16_t>(stream),
                                                std::istream_iterator<uint16_t>());

            if (values.size() != 32 + 1) {
                throw std::runtime_error("Wrong led size, got " + std::to_string(values.size()));
            }

            for (uint32_t i = 0; i < values.size() - 1; ++i) {
                if (values[i] < 0 || values[i] > 255) {
                    throw std::runtime_error("Wrong brightness value, got " + std::to_string(values[i]));
                }

                bytes.emplace_back((uint8_t)values[i]);
            }

            uint16_t duration = values[values.size() - 1];
            bytes.push_back(duration >> 8);
            bytes.push_back((uint8_t)duration);
        }

        std::string outputName = patternName.substr(0, patternName.find('.')) + ".bin";
        std::ofstream outputFile;
        outputFile.open(outputName, std::ios::out | std::ios::binary);

        for (auto b : bytes) {
            outputFile << b;
        }

        outputFile.close();

        std::cout << "\nGenerated file " << outputName << "\n";

    } else {
        throw std::runtime_error("Cannot open LED pattern file '" + patternName + "'");
    }

    return 0;
}
