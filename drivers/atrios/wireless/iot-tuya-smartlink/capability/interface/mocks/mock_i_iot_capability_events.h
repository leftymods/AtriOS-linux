#pragma once

#include <smart_devices/libs/iot/capability/interface/i_iot_capability_events.h>

#include <library/cpp/testing/gmock_in_unittest/gmock.h>

namespace YandexIO {

    class MockIIotCapabilityEvents: public IIotCapabilityEvents {
    public:
        MOCK_METHOD(void, onIotCapabilityDiscoveryStarted, (), (override));
        MOCK_METHOD(void, onIotCapabilityDiscoveryStopped, (), (override));
        MOCK_METHOD(void, onIotCapabilityWifiCredentialsReceived, (), (override));
        MOCK_METHOD(void, onIotCapabilityOff, (), (override));
    };

} // namespace YandexIO
