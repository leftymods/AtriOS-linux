#pragma once

#include "hci_command.h"
#include "hci_command_handler.h"

namespace telink {

    struct MfgEui64Request: public HciCommand<0x0800> {
    };

    struct MfgEui64Response: public HciCommand<0x8800> {
        uint8_t eui64[8];

    public:
        bool deserialize(std::span<const uint8_t> payload);
    };

    struct MfgStartRequest: public HciCommand<0x0801> {
    };

    struct MfgStopRequest: public HciCommand<0x0802> {
    };

    struct MfgSetChannelRequest: public HciCommand<0x0803> {
        uint8_t channel;

    public:
        std::vector<uint8_t> serialize() const;
    };

    struct MfgSetPowerRequest: public HciCommand<0x0804> {
        int8_t power;

    public:
        std::vector<uint8_t> serialize() const;
    };

    struct MfgSendPacketRequest: public HciCommand<0x0805> {
        std::vector<uint8_t> data;

    public:
        std::vector<uint8_t> serialize() const;
    };

    struct MfgReceivePacketIndication: public HciCommand<0x8806> {
        uint8_t lqi;
        int8_t rssi;
        uint8_t length;
        std::vector<uint8_t> data;

    public:
        bool deserialize(std::span<const uint8_t> payload);
    };

    template <class T>
    using MfgCommandHandler = HciCommandHandler<
        T,
        MfgEui64Response,
        MfgReceivePacketIndication>;

} // namespace telink
