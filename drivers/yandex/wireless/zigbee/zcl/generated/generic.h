#pragma once

#include <smart_devices/libs/zigbee/zigbee.h>
#include <smart_devices/libs/zigbee/zcl/i_cluster.h>
#include <smart_devices/libs/zigbee/zcl/types.h>

namespace zigbee {

    class Endpoint;

} // namespace zigbee

namespace zigbee::zcl {

class GenericCluster : public ICluster {
public:
    enum class CommandId : uint8_t {
        ReadAttributes = 0x00,
        ReadAttributesResponse = 0x01,
        WriteAttributes = 0x02,
        WriteAttributesUndivided = 0x03,
        WriteAttributesResponse = 0x04,
        WriteAttributesNoResponse = 0x05,
        ConfigureReporting = 0x06,
        ConfigureReportingResponse = 0x07,
        ReadReportingConfiguration = 0x08,
        ReadReportingConfigurationResponse = 0x09,
        ReportAttributes = 0x0A,
        DefaultResponse = 0x0B,
        DiscoverAttributes = 0x0C,
        DiscoverAttributesResponse = 0x0D,
        ReadAttributesStructured = 0x0E,
        WriteAttributesStructured = 0x0F,
        WriteAttributesStructuredResponse = 0x10,
        DiscoverCommandsReceived = 0x11,
        DiscoverCommandsReceivedResponse = 0x12,
        DiscoverCommandsGenerated = 0x13,
        DiscoverCommandsGeneratedResponse = 0x14,
        DiscoveAttributesExtended = 0x15,
        DiscoverAttributesExtendedResponse = 0x16,
    };

public:
    GenericCluster(std::weak_ptr<Endpoint> endpoint, uint16_t clusterId, ZclDirection direction);
    GenericCluster(std::weak_ptr<Endpoint> endpoint, uint16_t clusterId, std::optional<uint16_t> manufacturerCode, ZclDirection direction);

    uint16_t getClusterId() const override;
    std::optional<uint16_t> getManufacturerCode() const override;
    ZclDirection getDirection() const override;

    void handleCommand(const ZclHeader& header, const std::vector<uint8_t>& payload) override;

    void bind() override;

protected:
    void sendCommand(ZclFrameType frameType, uint8_t commandId, const std::vector<uint8_t>& payload) override;

private:
    void handleGlobalCommand(const ZclHeader& header, ZclStream& stream);
    virtual void handleClusterSpecificCommand(const ZclHeader& header, ZclStream& stream);

public:
    void readAttributes(
        std::vector<zcl::AttribId> attributeIdentifiers
    );

    void readAttributesResponse(
        std::vector<zcl::ReadAttributeResponseRecord> readAttributeRecords
    );

    void writeAttributes(
        std::vector<zcl::WriteAttributeRecord> writeAttributeRecords
    );

    void writeAttributesUndivided(
        std::vector<zcl::WriteAttributeRecord> writeAttributeRecords
    );

    void writeAttributesResponse(
        std::vector<zcl::WriteAttributeResponseRecord> writeAttributeRecords
    );

    void writeAttributesNoResponse(
        std::vector<zcl::WriteAttributeRecord> writeAttributeRecords
    );

    void configureReporting(
        std::vector<zcl::ConfigureReportingRecord> configureReportingRecords
    );

    void configureReportingResponse(
        std::vector<zcl::ConfigureReportingResponseRecord> configureReportingResponseRecords
    );

    void readReportingConfiguration(
        std::vector<zcl::ReadReportingConfigurationRecord> readReportingConfigurationRecords
    );

    void readReportingConfigurationResponse(
        std::vector<zcl::ReadReportingConfigurationResponseRecord> readReportingConfigurationResponseRecords
    );

    void reportAttributes(
        std::vector<zcl::AttributeReportRecord> attributeReportRecords
    );

    void defaultResponse(
        zcl::Uint8 commandIdentifier,
        zcl::ZclStatus statusCode
    );

    void discoverAttributes(
        zcl::AttribId startAttributeIdentifier,
        zcl::Uint8 maximumAttributeIdentifiers
    );

    void discoverAttributesResponse(
        zcl::Bool discoveryComplete,
        std::vector<zcl::DiscoverAttributesResponseRecord> attributeRecords
    );

    void readAttributesStructured(
        std::vector<zcl::ReadStructuredRecord> attributeRecords
    );

    void writeAttributesStructured(
        std::vector<zcl::WriteStructuredRecord> attributeRecords
    );

    void writeAttributesStructuredResponse(
        std::vector<zcl::WriteStructuredResponseRecord> attributeRecords
    );

    void discoverCommandsReceived(
        zcl::Uint8 startCommandIdentifier,
        zcl::Uint8 maximumCommandIdentifiers
    );

    void discoverCommandsReceivedResponse(
        zcl::Bool discoveryComplete,
        std::vector<zcl::Uint8> commandIdentifiers
    );

    void discoverCommandsGenerated(
        zcl::Uint8 startCommandIdentifier,
        zcl::Uint8 maximumCommandIdentifiers
    );

    void discoverCommandsGeneratedResponse(
        zcl::Bool discoveryComplete,
        std::vector<zcl::Uint8> commandIdentifiers
    );

    void discoveAttributesExtended(
        zcl::AttribId startAttributeIdentifier,
        zcl::Uint8 maximumAttributeIdentifiers
    );

    void discoverAttributesExtendedResponse(
        zcl::Bool discoveryComplete,
        std::vector<zcl::DiscoverAttributesExtendedResponseRecord> attributeRecords
    );


public:
    virtual void onReadAttributes(
        std::vector<zcl::AttribId> attributeIdentifiers
    );

    virtual void onReadAttributesResponse(
        std::vector<zcl::ReadAttributeResponseRecord> readAttributeRecords
    );

    virtual void onWriteAttributes(
        std::vector<zcl::WriteAttributeRecord> writeAttributeRecords
    );

    virtual void onWriteAttributesUndivided(
        std::vector<zcl::WriteAttributeRecord> writeAttributeRecords
    );

    virtual void onWriteAttributesResponse(
        std::vector<zcl::WriteAttributeResponseRecord> writeAttributeRecords
    );

    virtual void onWriteAttributesNoResponse(
        std::vector<zcl::WriteAttributeRecord> writeAttributeRecords
    );

    virtual void onConfigureReporting(
        std::vector<zcl::ConfigureReportingRecord> configureReportingRecords
    );

    virtual void onConfigureReportingResponse(
        std::vector<zcl::ConfigureReportingResponseRecord> configureReportingResponseRecords
    );

    virtual void onReadReportingConfiguration(
        std::vector<zcl::ReadReportingConfigurationRecord> readReportingConfigurationRecords
    );

    virtual void onReadReportingConfigurationResponse(
        std::vector<zcl::ReadReportingConfigurationResponseRecord> readReportingConfigurationResponseRecords
    );

    virtual void onReportAttributes(
        std::vector<zcl::AttributeReportRecord> attributeReportRecords
    );

    virtual void onDefaultResponse(
        zcl::Uint8 commandIdentifier,
        zcl::ZclStatus statusCode
    );

    virtual void onDiscoverAttributes(
        zcl::AttribId startAttributeIdentifier,
        zcl::Uint8 maximumAttributeIdentifiers
    );

    virtual void onDiscoverAttributesResponse(
        zcl::Bool discoveryComplete,
        std::vector<zcl::DiscoverAttributesResponseRecord> attributeRecords
    );

    virtual void onReadAttributesStructured(
        std::vector<zcl::ReadStructuredRecord> attributeRecords
    );

    virtual void onWriteAttributesStructured(
        std::vector<zcl::WriteStructuredRecord> attributeRecords
    );

    virtual void onWriteAttributesStructuredResponse(
        std::vector<zcl::WriteStructuredResponseRecord> attributeRecords
    );

    virtual void onDiscoverCommandsReceived(
        zcl::Uint8 startCommandIdentifier,
        zcl::Uint8 maximumCommandIdentifiers
    );

    virtual void onDiscoverCommandsReceivedResponse(
        zcl::Bool discoveryComplete,
        std::vector<zcl::Uint8> commandIdentifiers
    );

    virtual void onDiscoverCommandsGenerated(
        zcl::Uint8 startCommandIdentifier,
        zcl::Uint8 maximumCommandIdentifiers
    );

    virtual void onDiscoverCommandsGeneratedResponse(
        zcl::Bool discoveryComplete,
        std::vector<zcl::Uint8> commandIdentifiers
    );

    virtual void onDiscoveAttributesExtended(
        zcl::AttribId startAttributeIdentifier,
        zcl::Uint8 maximumAttributeIdentifiers
    );

    virtual void onDiscoverAttributesExtendedResponse(
        zcl::Bool discoveryComplete,
        std::vector<zcl::DiscoverAttributesExtendedResponseRecord> attributeRecords
    );


private:
    std::weak_ptr<Endpoint> endpoint_;

    uint16_t clusterId_;
    std::optional<uint16_t> manufacturerCode_;
    ZclDirection direction_;
};

} // namespace zigbee::zcl
