#pragma once

namespace YandexIO {

    class IIotCapabilityEvents {
    public:
        virtual ~IIotCapabilityEvents();

        virtual void onIotCapabilityDiscoveryStarted() = 0;
        virtual void onIotCapabilityDiscoveryStopped() = 0;
        virtual void onIotCapabilityWifiCredentialsReceived() = 0;
        virtual void onIotCapabilityOff() = 0;
    };

} // namespace YandexIO
