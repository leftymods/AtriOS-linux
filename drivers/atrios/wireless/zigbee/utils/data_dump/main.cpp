#include <smart_devices/libs/zigbee/proto/network.pb.h>

#include <util/stream/file.h>

#include <google/protobuf/text_format.h>

#include <string>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "Usage: data_dump <path_to_zigbee.dat>" << std::endl;
        exit(1);
    }

    TFileInput f(argv[1]);
    const TString networkData = f.ReadAll();

    zigbee::proto::NetworkParameters proto;
    Y_PROTOBUF_SUPPRESS_NODISCARD proto.ParseFromString(networkData);

    TString result;
    google::protobuf::TextFormat::PrintToString(proto, &result);

    std::cout << result << std::endl;
}