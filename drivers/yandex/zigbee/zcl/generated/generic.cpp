#include "generic.h"

#include <smart_devices/libs/zigbee/endpoint.h>

using namespace zigbee;
using namespace zigbee::zcl;

GenericCluster::GenericCluster(std::weak_ptr<Endpoint> endpoint, uint16_t clusterId, ZclDirection direction)
    : GenericCluster(endpoint, clusterId, std::nullopt, direction)
{
}

GenericCluster::GenericCluster(std::weak_ptr<Endpoint> endpoint, uint16_t clusterId, std::optional<uint16_t> manufacturerCode, ZclDirection direction)
    : endpoint_(endpoint)
    , clusterId_(clusterId)
    , manufacturerCode_(manufacturerCode)
    , direction_(direction)
{
}

uint16_t GenericCluster::getClusterId() const {
    return clusterId_;
}

std::optional<uint16_t> GenericCluster::getManufacturerCode() const {
    return manufacturerCode_;
}

ZclDirection GenericCluster::getDirection() const {
    return direction_;
}

void GenericCluster::handleCommand(const ZclHeader& header, const std::vector<uint8_t>& payload) {
    ZclStream stream(payload);

    switch (header.getFrameType()) {
        case zigbee::ZclFrameType::GLOBAL:
            handleGlobalCommand(header, stream);
            break;
        case zigbee::ZclFrameType::CLUSTER_SPECIFIC:
            handleClusterSpecificCommand(header, stream);
            break;
    }
}

void GenericCluster::bind() {
    if (auto endpoint = endpoint_.lock()) {
        endpoint->bind(clusterId_);
    }
}

void GenericCluster::sendCommand(ZclFrameType frameType, uint8_t commandId, const std::vector<uint8_t>& payload) {
    if (auto endpoint = endpoint_.lock()) {
        ZclHeader header = {};
        if (frameType == ZclFrameType::CLUSTER_SPECIFIC) {
            header.frameControl |= 0b01;
        }
        if (manufacturerCode_) {
            header.frameControl |= 0b100;
            header.manufacturerCode = *manufacturerCode_;
        }
        header.commandId = commandId;

        endpoint->sendMessage(clusterId_, header, payload);
    }
}

void GenericCluster::handleGlobalCommand(const ZclHeader& header, ZclStream& stream) {
    switch (static_cast<CommandId>(header.commandId)) {
        case CommandId::ReadAttributes: {
            std::vector<AttribId> attributeIdentifiers;

            attributeIdentifiers = stream.readArray<AttribId, 0>();

            onReadAttributes(
                std::forward<std::vector<AttribId>>(attributeIdentifiers)
            );
            break;
        }
        case CommandId::ReadAttributesResponse: {
            std::vector<ReadAttributeResponseRecord> readAttributeRecords;

            readAttributeRecords = stream.readArray<ReadAttributeResponseRecord, 0>();

            onReadAttributesResponse(
                std::forward<std::vector<ReadAttributeResponseRecord>>(readAttributeRecords)
            );
            break;
        }
        case CommandId::WriteAttributes: {
            std::vector<WriteAttributeRecord> writeAttributeRecords;

            writeAttributeRecords = stream.readArray<WriteAttributeRecord, 0>();

            onWriteAttributes(
                std::forward<std::vector<WriteAttributeRecord>>(writeAttributeRecords)
            );
            break;
        }
        case CommandId::WriteAttributesUndivided: {
            std::vector<WriteAttributeRecord> writeAttributeRecords;

            writeAttributeRecords = stream.readArray<WriteAttributeRecord, 0>();

            onWriteAttributesUndivided(
                std::forward<std::vector<WriteAttributeRecord>>(writeAttributeRecords)
            );
            break;
        }
        case CommandId::WriteAttributesResponse: {
            std::vector<WriteAttributeResponseRecord> writeAttributeRecords;

            writeAttributeRecords = stream.readArray<WriteAttributeResponseRecord, 0>();

            onWriteAttributesResponse(
                std::forward<std::vector<WriteAttributeResponseRecord>>(writeAttributeRecords)
            );
            break;
        }
        case CommandId::WriteAttributesNoResponse: {
            std::vector<WriteAttributeRecord> writeAttributeRecords;

            writeAttributeRecords = stream.readArray<WriteAttributeRecord, 0>();

            onWriteAttributesNoResponse(
                std::forward<std::vector<WriteAttributeRecord>>(writeAttributeRecords)
            );
            break;
        }
        case CommandId::ConfigureReporting: {
            std::vector<ConfigureReportingRecord> configureReportingRecords;

            configureReportingRecords = stream.readArray<ConfigureReportingRecord, 0>();

            onConfigureReporting(
                std::forward<std::vector<ConfigureReportingRecord>>(configureReportingRecords)
            );
            break;
        }
        case CommandId::ConfigureReportingResponse: {
            std::vector<ConfigureReportingResponseRecord> configureReportingResponseRecords;

            configureReportingResponseRecords = stream.readArray<ConfigureReportingResponseRecord, 0>();

            onConfigureReportingResponse(
                std::forward<std::vector<ConfigureReportingResponseRecord>>(configureReportingResponseRecords)
            );
            break;
        }
        case CommandId::ReadReportingConfiguration: {
            std::vector<ReadReportingConfigurationRecord> readReportingConfigurationRecords;

            readReportingConfigurationRecords = stream.readArray<ReadReportingConfigurationRecord, 0>();

            onReadReportingConfiguration(
                std::forward<std::vector<ReadReportingConfigurationRecord>>(readReportingConfigurationRecords)
            );
            break;
        }
        case CommandId::ReadReportingConfigurationResponse: {
            std::vector<ReadReportingConfigurationResponseRecord> readReportingConfigurationResponseRecords;

            readReportingConfigurationResponseRecords = stream.readArray<ReadReportingConfigurationResponseRecord, 0>();

            onReadReportingConfigurationResponse(
                std::forward<std::vector<ReadReportingConfigurationResponseRecord>>(readReportingConfigurationResponseRecords)
            );
            break;
        }
        case CommandId::ReportAttributes: {
            std::vector<AttributeReportRecord> attributeReportRecords;

            attributeReportRecords = stream.readArray<AttributeReportRecord, 0>();

            onReportAttributes(
                std::forward<std::vector<AttributeReportRecord>>(attributeReportRecords)
            );
            break;
        }
        case CommandId::DefaultResponse: {
            Uint8 commandIdentifier;
            ZclStatus statusCode;

            commandIdentifier = stream.read<Uint8>();
            statusCode = stream.read<ZclStatus>();

            onDefaultResponse(
                std::forward<Uint8>(commandIdentifier),
                std::forward<ZclStatus>(statusCode)
            );
            break;
        }
        case CommandId::DiscoverAttributes: {
            AttribId startAttributeIdentifier;
            Uint8 maximumAttributeIdentifiers;

            startAttributeIdentifier = stream.read<AttribId>();
            maximumAttributeIdentifiers = stream.read<Uint8>();

            onDiscoverAttributes(
                std::forward<AttribId>(startAttributeIdentifier),
                std::forward<Uint8>(maximumAttributeIdentifiers)
            );
            break;
        }
        case CommandId::DiscoverAttributesResponse: {
            Bool discoveryComplete;
            std::vector<DiscoverAttributesResponseRecord> attributeRecords;

            discoveryComplete = stream.read<Bool>();
            attributeRecords = stream.readArray<DiscoverAttributesResponseRecord, 0>();

            onDiscoverAttributesResponse(
                std::forward<Bool>(discoveryComplete),
                std::forward<std::vector<DiscoverAttributesResponseRecord>>(attributeRecords)
            );
            break;
        }
        case CommandId::ReadAttributesStructured: {
            std::vector<ReadStructuredRecord> attributeRecords;

            attributeRecords = stream.readArray<ReadStructuredRecord, 0>();

            onReadAttributesStructured(
                std::forward<std::vector<ReadStructuredRecord>>(attributeRecords)
            );
            break;
        }
        case CommandId::WriteAttributesStructured: {
            std::vector<WriteStructuredRecord> attributeRecords;

            attributeRecords = stream.readArray<WriteStructuredRecord, 0>();

            onWriteAttributesStructured(
                std::forward<std::vector<WriteStructuredRecord>>(attributeRecords)
            );
            break;
        }
        case CommandId::WriteAttributesStructuredResponse: {
            std::vector<WriteStructuredResponseRecord> attributeRecords;

            attributeRecords = stream.readArray<WriteStructuredResponseRecord, 0>();

            onWriteAttributesStructuredResponse(
                std::forward<std::vector<WriteStructuredResponseRecord>>(attributeRecords)
            );
            break;
        }
        case CommandId::DiscoverCommandsReceived: {
            Uint8 startCommandIdentifier;
            Uint8 maximumCommandIdentifiers;

            startCommandIdentifier = stream.read<Uint8>();
            maximumCommandIdentifiers = stream.read<Uint8>();

            onDiscoverCommandsReceived(
                std::forward<Uint8>(startCommandIdentifier),
                std::forward<Uint8>(maximumCommandIdentifiers)
            );
            break;
        }
        case CommandId::DiscoverCommandsReceivedResponse: {
            Bool discoveryComplete;
            std::vector<Uint8> commandIdentifiers;

            discoveryComplete = stream.read<Bool>();
            commandIdentifiers = stream.readArray<Uint8, 0>();

            onDiscoverCommandsReceivedResponse(
                std::forward<Bool>(discoveryComplete),
                std::forward<std::vector<Uint8>>(commandIdentifiers)
            );
            break;
        }
        case CommandId::DiscoverCommandsGenerated: {
            Uint8 startCommandIdentifier;
            Uint8 maximumCommandIdentifiers;

            startCommandIdentifier = stream.read<Uint8>();
            maximumCommandIdentifiers = stream.read<Uint8>();

            onDiscoverCommandsGenerated(
                std::forward<Uint8>(startCommandIdentifier),
                std::forward<Uint8>(maximumCommandIdentifiers)
            );
            break;
        }
        case CommandId::DiscoverCommandsGeneratedResponse: {
            Bool discoveryComplete;
            std::vector<Uint8> commandIdentifiers;

            discoveryComplete = stream.read<Bool>();
            commandIdentifiers = stream.readArray<Uint8, 0>();

            onDiscoverCommandsGeneratedResponse(
                std::forward<Bool>(discoveryComplete),
                std::forward<std::vector<Uint8>>(commandIdentifiers)
            );
            break;
        }
        case CommandId::DiscoveAttributesExtended: {
            AttribId startAttributeIdentifier;
            Uint8 maximumAttributeIdentifiers;

            startAttributeIdentifier = stream.read<AttribId>();
            maximumAttributeIdentifiers = stream.read<Uint8>();

            onDiscoveAttributesExtended(
                std::forward<AttribId>(startAttributeIdentifier),
                std::forward<Uint8>(maximumAttributeIdentifiers)
            );
            break;
        }
        case CommandId::DiscoverAttributesExtendedResponse: {
            Bool discoveryComplete;
            std::vector<DiscoverAttributesExtendedResponseRecord> attributeRecords;

            discoveryComplete = stream.read<Bool>();
            attributeRecords = stream.readArray<DiscoverAttributesExtendedResponseRecord, 0>();

            onDiscoverAttributesExtendedResponse(
                std::forward<Bool>(discoveryComplete),
                std::forward<std::vector<DiscoverAttributesExtendedResponseRecord>>(attributeRecords)
            );
            break;
        }
        default:
            break;
    }
}

void GenericCluster::handleClusterSpecificCommand(const ZclHeader& header, ZclStream& stream) {
    Y_UNUSED(header);
    Y_UNUSED(stream);
}

void GenericCluster::readAttributes(
    std::vector<AttribId> attributeIdentifiers
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.writeArray<AttribId, 0>(attributeIdentifiers);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::ReadAttributes), buffer);
}

void GenericCluster::readAttributesResponse(
    std::vector<ReadAttributeResponseRecord> readAttributeRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.writeArray<ReadAttributeResponseRecord, 0>(readAttributeRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::ReadAttributesResponse), buffer);
}

void GenericCluster::writeAttributes(
    std::vector<WriteAttributeRecord> writeAttributeRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.writeArray<WriteAttributeRecord, 0>(writeAttributeRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::WriteAttributes), buffer);
}

void GenericCluster::writeAttributesUndivided(
    std::vector<WriteAttributeRecord> writeAttributeRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.writeArray<WriteAttributeRecord, 0>(writeAttributeRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::WriteAttributesUndivided), buffer);
}

void GenericCluster::writeAttributesResponse(
    std::vector<WriteAttributeResponseRecord> writeAttributeRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.writeArray<WriteAttributeResponseRecord, 0>(writeAttributeRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::WriteAttributesResponse), buffer);
}

void GenericCluster::writeAttributesNoResponse(
    std::vector<WriteAttributeRecord> writeAttributeRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.writeArray<WriteAttributeRecord, 0>(writeAttributeRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::WriteAttributesNoResponse), buffer);
}

void GenericCluster::configureReporting(
    std::vector<ConfigureReportingRecord> configureReportingRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.writeArray<ConfigureReportingRecord, 0>(configureReportingRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::ConfigureReporting), buffer);
}

void GenericCluster::configureReportingResponse(
    std::vector<ConfigureReportingResponseRecord> configureReportingResponseRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.writeArray<ConfigureReportingResponseRecord, 0>(configureReportingResponseRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::ConfigureReportingResponse), buffer);
}

void GenericCluster::readReportingConfiguration(
    std::vector<ReadReportingConfigurationRecord> readReportingConfigurationRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.writeArray<ReadReportingConfigurationRecord, 0>(readReportingConfigurationRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::ReadReportingConfiguration), buffer);
}

void GenericCluster::readReportingConfigurationResponse(
    std::vector<ReadReportingConfigurationResponseRecord> readReportingConfigurationResponseRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.writeArray<ReadReportingConfigurationResponseRecord, 0>(readReportingConfigurationResponseRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::ReadReportingConfigurationResponse), buffer);
}

void GenericCluster::reportAttributes(
    std::vector<AttributeReportRecord> attributeReportRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.writeArray<AttributeReportRecord, 0>(attributeReportRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::ReportAttributes), buffer);
}

void GenericCluster::defaultResponse(
    Uint8 commandIdentifier,
    ZclStatus statusCode
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint8>(commandIdentifier);
    stream.write<ZclStatus>(statusCode);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::DefaultResponse), buffer);
}

void GenericCluster::discoverAttributes(
    AttribId startAttributeIdentifier,
    Uint8 maximumAttributeIdentifiers
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<AttribId>(startAttributeIdentifier);
    stream.write<Uint8>(maximumAttributeIdentifiers);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::DiscoverAttributes), buffer);
}

void GenericCluster::discoverAttributesResponse(
    Bool discoveryComplete,
    std::vector<DiscoverAttributesResponseRecord> attributeRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Bool>(discoveryComplete);
    stream.writeArray<DiscoverAttributesResponseRecord, 0>(attributeRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::DiscoverAttributesResponse), buffer);
}

void GenericCluster::readAttributesStructured(
    std::vector<ReadStructuredRecord> attributeRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.writeArray<ReadStructuredRecord, 0>(attributeRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::ReadAttributesStructured), buffer);
}

void GenericCluster::writeAttributesStructured(
    std::vector<WriteStructuredRecord> attributeRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.writeArray<WriteStructuredRecord, 0>(attributeRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::WriteAttributesStructured), buffer);
}

void GenericCluster::writeAttributesStructuredResponse(
    std::vector<WriteStructuredResponseRecord> attributeRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.writeArray<WriteStructuredResponseRecord, 0>(attributeRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::WriteAttributesStructuredResponse), buffer);
}

void GenericCluster::discoverCommandsReceived(
    Uint8 startCommandIdentifier,
    Uint8 maximumCommandIdentifiers
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint8>(startCommandIdentifier);
    stream.write<Uint8>(maximumCommandIdentifiers);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::DiscoverCommandsReceived), buffer);
}

void GenericCluster::discoverCommandsReceivedResponse(
    Bool discoveryComplete,
    std::vector<Uint8> commandIdentifiers
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Bool>(discoveryComplete);
    stream.writeArray<Uint8, 0>(commandIdentifiers);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::DiscoverCommandsReceivedResponse), buffer);
}

void GenericCluster::discoverCommandsGenerated(
    Uint8 startCommandIdentifier,
    Uint8 maximumCommandIdentifiers
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Uint8>(startCommandIdentifier);
    stream.write<Uint8>(maximumCommandIdentifiers);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::DiscoverCommandsGenerated), buffer);
}

void GenericCluster::discoverCommandsGeneratedResponse(
    Bool discoveryComplete,
    std::vector<Uint8> commandIdentifiers
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Bool>(discoveryComplete);
    stream.writeArray<Uint8, 0>(commandIdentifiers);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::DiscoverCommandsGeneratedResponse), buffer);
}

void GenericCluster::discoveAttributesExtended(
    AttribId startAttributeIdentifier,
    Uint8 maximumAttributeIdentifiers
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<AttribId>(startAttributeIdentifier);
    stream.write<Uint8>(maximumAttributeIdentifiers);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::DiscoveAttributesExtended), buffer);
}

void GenericCluster::discoverAttributesExtendedResponse(
    Bool discoveryComplete,
    std::vector<DiscoverAttributesExtendedResponseRecord> attributeRecords
) {
    std::vector<uint8_t> buffer;
    ZclOutputStream stream(buffer);

    stream.write<Bool>(discoveryComplete);
    stream.writeArray<DiscoverAttributesExtendedResponseRecord, 0>(attributeRecords);

    sendCommand(ZclFrameType::GLOBAL, static_cast<uint8_t>(CommandId::DiscoverAttributesExtendedResponse), buffer);
}


void GenericCluster::onReadAttributes(
    std::vector<AttribId> attributeIdentifiers
) {
    Y_UNUSED(attributeIdentifiers);
}

void GenericCluster::onReadAttributesResponse(
    std::vector<ReadAttributeResponseRecord> readAttributeRecords
) {
    Y_UNUSED(readAttributeRecords);
}

void GenericCluster::onWriteAttributes(
    std::vector<WriteAttributeRecord> writeAttributeRecords
) {
    Y_UNUSED(writeAttributeRecords);
}

void GenericCluster::onWriteAttributesUndivided(
    std::vector<WriteAttributeRecord> writeAttributeRecords
) {
    Y_UNUSED(writeAttributeRecords);
}

void GenericCluster::onWriteAttributesResponse(
    std::vector<WriteAttributeResponseRecord> writeAttributeRecords
) {
    Y_UNUSED(writeAttributeRecords);
}

void GenericCluster::onWriteAttributesNoResponse(
    std::vector<WriteAttributeRecord> writeAttributeRecords
) {
    Y_UNUSED(writeAttributeRecords);
}

void GenericCluster::onConfigureReporting(
    std::vector<ConfigureReportingRecord> configureReportingRecords
) {
    Y_UNUSED(configureReportingRecords);
}

void GenericCluster::onConfigureReportingResponse(
    std::vector<ConfigureReportingResponseRecord> configureReportingResponseRecords
) {
    Y_UNUSED(configureReportingResponseRecords);
}

void GenericCluster::onReadReportingConfiguration(
    std::vector<ReadReportingConfigurationRecord> readReportingConfigurationRecords
) {
    Y_UNUSED(readReportingConfigurationRecords);
}

void GenericCluster::onReadReportingConfigurationResponse(
    std::vector<ReadReportingConfigurationResponseRecord> readReportingConfigurationResponseRecords
) {
    Y_UNUSED(readReportingConfigurationResponseRecords);
}

void GenericCluster::onReportAttributes(
    std::vector<AttributeReportRecord> attributeReportRecords
) {
    Y_UNUSED(attributeReportRecords);
}

void GenericCluster::onDefaultResponse(
    Uint8 commandIdentifier,
    ZclStatus statusCode
) {
    Y_UNUSED(commandIdentifier);
    Y_UNUSED(statusCode);
}

void GenericCluster::onDiscoverAttributes(
    AttribId startAttributeIdentifier,
    Uint8 maximumAttributeIdentifiers
) {
    Y_UNUSED(startAttributeIdentifier);
    Y_UNUSED(maximumAttributeIdentifiers);
}

void GenericCluster::onDiscoverAttributesResponse(
    Bool discoveryComplete,
    std::vector<DiscoverAttributesResponseRecord> attributeRecords
) {
    Y_UNUSED(discoveryComplete);
    Y_UNUSED(attributeRecords);
}

void GenericCluster::onReadAttributesStructured(
    std::vector<ReadStructuredRecord> attributeRecords
) {
    Y_UNUSED(attributeRecords);
}

void GenericCluster::onWriteAttributesStructured(
    std::vector<WriteStructuredRecord> attributeRecords
) {
    Y_UNUSED(attributeRecords);
}

void GenericCluster::onWriteAttributesStructuredResponse(
    std::vector<WriteStructuredResponseRecord> attributeRecords
) {
    Y_UNUSED(attributeRecords);
}

void GenericCluster::onDiscoverCommandsReceived(
    Uint8 startCommandIdentifier,
    Uint8 maximumCommandIdentifiers
) {
    Y_UNUSED(startCommandIdentifier);
    Y_UNUSED(maximumCommandIdentifiers);
}

void GenericCluster::onDiscoverCommandsReceivedResponse(
    Bool discoveryComplete,
    std::vector<Uint8> commandIdentifiers
) {
    Y_UNUSED(discoveryComplete);
    Y_UNUSED(commandIdentifiers);
}

void GenericCluster::onDiscoverCommandsGenerated(
    Uint8 startCommandIdentifier,
    Uint8 maximumCommandIdentifiers
) {
    Y_UNUSED(startCommandIdentifier);
    Y_UNUSED(maximumCommandIdentifiers);
}

void GenericCluster::onDiscoverCommandsGeneratedResponse(
    Bool discoveryComplete,
    std::vector<Uint8> commandIdentifiers
) {
    Y_UNUSED(discoveryComplete);
    Y_UNUSED(commandIdentifiers);
}

void GenericCluster::onDiscoveAttributesExtended(
    AttribId startAttributeIdentifier,
    Uint8 maximumAttributeIdentifiers
) {
    Y_UNUSED(startAttributeIdentifier);
    Y_UNUSED(maximumAttributeIdentifiers);
}

void GenericCluster::onDiscoverAttributesExtendedResponse(
    Bool discoveryComplete,
    std::vector<DiscoverAttributesExtendedResponseRecord> attributeRecords
) {
    Y_UNUSED(discoveryComplete);
    Y_UNUSED(attributeRecords);
}

