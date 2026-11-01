#pragma once

class ILightSensor {
public:
    virtual float getBrightness() = 0;

    virtual ~ILightSensor();
};
