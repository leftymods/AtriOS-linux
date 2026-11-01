#pragma once

#include <string>

class IVolumeManagerListener {
public:
    virtual ~IVolumeManagerListener();

    virtual void onVolumeChange(int platformVolume, int aliceVolume, bool isMuted, const std::string& source, bool setBtVolume) = 0;
};
