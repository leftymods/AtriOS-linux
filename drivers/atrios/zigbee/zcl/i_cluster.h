#pragma once

#include "zcl_stream.h"

#include <smart_devices/libs/zigbee/zigbee.h>

namespace zigbee::zcl {

    class ICluster {
    public:
        virtual ~ICluster();

        virtual uint16_t getClusterId() const = 0;
        virtual std::optional<uint16_t> getManufacturerCode() const = 0;
        virtual ZclDirection getDirection() const = 0;

        virtual void handleCommand(const ZclHeader& header, const std::vector<uint8_t>& payload) = 0;

        virtual void bind() = 0;

    protected:
        virtual void sendCommand(ZclFrameType frameType, uint8_t commandId, const std::vector<uint8_t>& payload) = 0;
    };

} // namespace zigbee::zcl
