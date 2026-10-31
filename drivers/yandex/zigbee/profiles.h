#pragma once

#include <cstdint>

namespace zigbee {

    struct ZHAProfile {
        static constexpr uint16_t Id = 0x0104;

        enum DeviceId: uint16_t {
            // Generic
            SmartPlug = 0x0051,

            // Lighting & Occupancy
            OnOffLight = 0x0100,
            DimmableLight = 0x0101,
            ColorDimmableLight = 0x0102,
            OnOffLightSwitch = 0x0103,
            DimmerSwitch = 0x0104,
            ColorDimmerSwitch = 0x0105,
            LightSensor = 0x0106,
            OccupancySensor = 0x0107,
            OnOffBallast = 0x0108,
            DimmableBallast = 0x0109,
            OnOffPlugInUnit = 0x010a,
            DimmablePlugInUnit = 0x010b,
            ColorTemperatureLight = 0x010c,
            ExtendedColorLight = 0x010d,
            LightLevelSensor = 0x010d,

            // HVAC
            TemperatureSensor = 0x0302,
            PressureSensor = 0x0305,
        };
    };

    struct ZLLProfile {
        static constexpr uint16_t Id = 0xc05e;

        enum DeviceId: uint16_t {
            OnOffLight = 0x0000,
            OnOffPlugInUnit = 0x0010,
            DimmableLight = 0x0100,
            DimmablePlugInUnit = 0x0110,
            ColorLight = 0x0200,
            ExtendedColorLight = 0x0210,
            ColorTemperatureLight = 0x0220,
        };
    };

} // namespace zigbee
