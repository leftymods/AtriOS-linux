#pragma once

#include <smart_devices/libs/zigbee/ncp/i_ncp.h>

#include <library/cpp/testing/gmock_in_unittest/gmock.h>

namespace zigbee {
    class MockNcp: public INcp {
    public:
        MockNcp();

        MOCK_METHOD(void, launchBootloader, (), (override));
        MOCK_METHOD(FirmwareVersion, getVersion, (), (override));
        MOCK_METHOD(const EUI64&, getEui64, (), (override));
        MOCK_METHOD(void, formNetwork, (const NetworkParameters& parameters), (override));
        MOCK_METHOD(void, leaveNetwork, (), (override));
        MOCK_METHOD(void, openNetwork, (uint8_t duration), (override));
        MOCK_METHOD(void, closeNetwork, (), (override));
        MOCK_METHOD(void, sendMessage, (NodeId destination, uint16_t profileId, uint16_t clusterId, uint8_t sourceEndpoint, uint8_t destinationEndpoint, const std::vector<uint8_t>& message), (override));
        MOCK_METHOD(void, sendZclCommand, (NodeId destination, uint16_t profileId, uint16_t clusterId, uint8_t sourceEndpoint, uint8_t destinationEndpoint, ZclFrameType frameType, std::optional<uint16_t> manufacturerCode, ZclDirection direction, uint8_t commandId, const std::vector<uint8_t>& payload), (override));
        MOCK_METHOD(void, requestActiveEndpoints, (NodeId target), (override));
        MOCK_METHOD(void, requestSimpleDescriptor, (NodeId target, uint8_t endpoint), (override));
        MOCK_METHOD(void, requestLeave, (NodeId target, EUI64 deviceAddress), (override));
        MOCK_METHOD(void, sendBindRequest, (NodeId target, const EUI64& source, uint8_t sourceEndpoint, uint16_t clusterId, uint8_t destinationEndpoint), (override));
        MOCK_METHOD(void, init, (), (override));
        MOCK_METHOD(void, setListener, (std::weak_ptr<INcpListener> listener), (override));
        MOCK_METHOD(Json::Value, collectMetrics, (), (override));

    private:
        std::weak_ptr<INcpListener> listener_;
    };
} // namespace zigbee
