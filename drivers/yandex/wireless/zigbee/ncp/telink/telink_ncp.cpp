#include "telink_ncp.h"

#include <smart_devices/libs/telink/hci/zigbee_commands.h>

#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("zigbee");

using namespace zigbee;

namespace {

    class TelinkNcpHandler: public telink::ZigbeeCommandHandler<TelinkNcpHandler> {
    public:
        TelinkNcpHandler(TelinkNcp& ncp)
            : ncp_(ncp)
        {
        }

        template <class Command>
        telink::HciStatus handle(Command cmd);

        template <>
        telink::HciStatus handle(telink::ZigbeeDiscoverySimpleDescRsp cmd) {
            YIO_LOG_DEBUG(
                "ZigbeeDiscoverySimpleDescRsp:"
                << " srcAddr " << quasar::Hex(cmd.srcAddr)
                << " seqNum " << quasar::Hex(cmd.seqNum)
                << " status " << quasar::Hex(cmd.status)
                << " nwkAddrOfInterest " << quasar::Hex(cmd.nwkAddrOfInterest)
                << " length " << quasar::Hex(cmd.length)
                << " simpleDesc " << quasar::Hex(cmd.simpleDesc));

            ncp_.callbackQueue_->add([this, cmd]() {
                if (cmd.simpleDesc.size() < 8) {
                    YIO_LOG_WARN("Malformed SimpleDescriptor");
                    return;
                }

                SimpleDescriptor descriptor;
                descriptor.endpoint = cmd.simpleDesc[0];
                descriptor.profileId = (cmd.simpleDesc[1] << 8) + cmd.simpleDesc[2];
                descriptor.deviceId = (cmd.simpleDesc[3] << 8) + cmd.simpleDesc[4];
                descriptor.deviceVersion = cmd.simpleDesc[5];

                size_t offset = 6;

                descriptor.inputClusters.resize(cmd.simpleDesc[offset++]);

                if (cmd.simpleDesc.size() < offset + 2 * descriptor.inputClusters.size() + 1) {
                    YIO_LOG_WARN("Malformed SimpleDescriptor");
                    return;
                }

                for (auto& cluster : descriptor.inputClusters) {
                    cluster = (cmd.simpleDesc[offset] << 8) + cmd.simpleDesc[offset + 1];
                    offset += 2;
                }

                descriptor.outputClusters.resize(cmd.simpleDesc[offset++]);

                if (cmd.simpleDesc.size() < offset + 2 * descriptor.outputClusters.size()) {
                    YIO_LOG_WARN("Malformed SimpleDescriptor");
                    return;
                }

                for (auto& cluster : descriptor.outputClusters) {
                    cluster = (cmd.simpleDesc[offset] << 8) + cmd.simpleDesc[offset + 1];
                    offset += 2;
                }

                if (auto listener = ncp_.listener_.lock()) {
                    listener->onSimpleDescriptor(cmd.nwkAddrOfInterest, descriptor);
                }
            });

            return telink::HciStatus::Success;
        }

        template <>
        telink::HciStatus handle(telink::ZigbeeDiscoveryActiveEpRsp cmd) {
            YIO_LOG_DEBUG(
                "ZigbeeDiscoveryActiveEpRsp:"
                << " srcAddr " << quasar::Hex(cmd.srcAddr)
                << " seqNum " << quasar::Hex(cmd.seqNum)
                << " status " << quasar::Hex(cmd.status)
                << " nwkAddrOfInterest " << quasar::Hex(cmd.nwkAddrOfInterest)
                << " activeEpCount " << quasar::Hex(cmd.activeEpCount)
                << " epList " << quasar::Hex(cmd.epList));

            ncp_.callbackQueue_->add([this, cmd]() {
                if (auto listener = ncp_.listener_.lock()) {
                    listener->onActiveEndpoints(cmd.nwkAddrOfInterest, cmd.epList);
                }
            });

            return telink::HciStatus::Success;
        }

        template <>
        telink::HciStatus handle(telink::ZigbeeNodesDevAnnceInd cmd) {
            YIO_LOG_DEBUG(
                "ZigbeeNodesDevAnnceInd:"
                << " nwkAttr " << quasar::Hex(cmd.nwkAddr)
                << " ieeeAddr " << HexEncode(cmd.ieeeAddr, sizeof(cmd.ieeeAddr))
                << " capability " << quasar::Hex(cmd.capability));

            ncp_.callbackQueue_->add([this, cmd]() {
                if (auto listener = ncp_.listener_.lock()) {
                    listener->onDeviceAnnounce(cmd.nwkAddr, std::to_array(cmd.ieeeAddr), cmd.capability);
                }
            });

            return telink::HciStatus::Success;
        }

        template <>
        telink::HciStatus handle(telink::ZigbeeGetLocalNwkInfoRsp cmd) {
            YIO_LOG_DEBUG(
                "ZigbeeGetLocalNwkInfoRsp:"
                << " devType " << quasar::Hex(cmd.devType)
                << " capability " << quasar::Hex(cmd.capability)
                << " onANetwork " << quasar::Hex(cmd.onANetwork)
                << " panId " << quasar::Hex(cmd.panId)
                << " extPanID " << HexEncode(cmd.extPanID, sizeof(cmd.extPanID))
                << " nwkAddr " << quasar::Hex(cmd.nwkAddr)
                << " ieeeAddr " << HexEncode(cmd.ieeeAddr, sizeof(cmd.ieeeAddr)));

            ncp_.callbackQueue_->add([this]() {
                ncp_.setState(NcpState::Initialized);
            });

            return telink::HciStatus::Success;
        }

        template <>
        telink::HciStatus handle(telink::ZigbeeZclReportMsgRcv cmd) {
            YIO_LOG_DEBUG(
                "ZigbeeZclReportMsgRcv:"
                << " srcAddr " << quasar::Hex(cmd.header.srcAddr)
                << " srcEp " << quasar::Hex(cmd.header.srcEp)
                << " dstEp " << quasar::Hex(cmd.header.dstEp)
                << " seqNum " << quasar::Hex(cmd.header.seqNum)
                << " cluster " << quasar::Hex(cmd.clusterId)
                << " attrNum " << quasar::Hex(cmd.attrNum)
                << " attrList " << quasar::Hex(cmd.attrList));

            ncp_.callbackQueue_->add([this, cmd]() {
                ZclHeader header{
                    .frameControl = 0b1000,
                    .manufacturerCode = 0,
                    .transactionSequenceNumber = cmd.header.seqNum,
                    .commandId = 0x0A,
                };

                if (auto listener = ncp_.listener_.lock()) {
                    listener->onZclMessage(
                        cmd.header.srcAddr,
                        cmd.header.srcEp,
                        0x0104,
                        cmd.clusterId,
                        header,
                        cmd.attrList);
                }
            });

            return telink::HciStatus::Success;
        }

    private:
        TelinkNcp& ncp_;
    };

} // namespace

TelinkNcp::TelinkNcp(const std::string& port)
    : port_(port)
    , callbackQueue_(std::make_unique<quasar::NamedCallbackQueue>("TelinkNcp"))
    , handler_(std::make_shared<TelinkNcpHandler>(*this))
    , state_(NcpState::Uninitialized)
{
}

TelinkNcp::~TelinkNcp() {
}

void TelinkNcp::init() {
    hci_ = std::make_unique<telink::Hci>(port_);

    setState(NcpState::Uninitialized);

    hci_->start(handler_);

    hci_->send(telink::ZigbeeGetLocalNwkInfoReq{});
}

void TelinkNcp::setListener(std::weak_ptr<INcpListener> listener) {
    callbackQueue_->add([this, listener]() {
        listener_ = listener;
    });
}

void TelinkNcp::launchBootloader() {
    YIO_LOG_WARN("Not implemented");
}

FirmwareVersion TelinkNcp::getVersion() {
    YIO_LOG_WARN("Not implemented");

    return FirmwareVersion{};
}

const EUI64& TelinkNcp::getEui64() {
    YIO_LOG_WARN("Not implemented");

    return eui64_;
}

void TelinkNcp::formNetwork(const NetworkParameters& parameters) {
    Y_UNUSED(parameters);

    YIO_LOG_WARN("Network parameters are ignored");

    hci_->send(telink::ZigbeeBdbCommissionFormation{});
}

void TelinkNcp::leaveNetwork() {
    YIO_LOG_WARN("Not implemented");
}

void TelinkNcp::openNetwork(uint8_t duration) {
    hci_->send(telink::ZigbeeMgmtPermitJoinReq{
        .dstAddr = 0xfffc,
        .permitDuration = duration,
        .tcSignificance = 1,
    });
}

void TelinkNcp::closeNetwork() {
    hci_->send(telink::ZigbeeMgmtPermitJoinReq{
        .dstAddr = 0xfffc,
        .permitDuration = 0,
        .tcSignificance = 1,
    });
}

void TelinkNcp::sendMessage(
    NodeId destination,
    uint16_t profileId,
    uint16_t clusterId,
    uint8_t sourceEndpoint,
    uint8_t destinationEndpoint,
    const std::vector<uint8_t>& message)
{
    Y_UNUSED(destination);
    Y_UNUSED(profileId);
    Y_UNUSED(clusterId);
    Y_UNUSED(sourceEndpoint);
    Y_UNUSED(destinationEndpoint);
    Y_UNUSED(message);

    YIO_LOG_WARN("Not implemented");
}

void TelinkNcp::sendZclCommand(
    NodeId destination,
    uint16_t profileId,
    uint16_t clusterId,
    uint8_t sourceEndpoint,
    uint8_t destinationEndpoint,
    ZclFrameType frameType,
    std::optional<uint16_t> manufacturerCode,
    ZclDirection direction,
    uint8_t commandId,
    const std::vector<uint8_t>& payload)
{
    Y_UNUSED(destination);
    Y_UNUSED(profileId);
    Y_UNUSED(clusterId);
    Y_UNUSED(sourceEndpoint);
    Y_UNUSED(destinationEndpoint);
    Y_UNUSED(frameType);
    Y_UNUSED(manufacturerCode);
    Y_UNUSED(direction);
    Y_UNUSED(commandId);
    Y_UNUSED(payload);

    YIO_LOG_WARN("Not implemented");
}

void TelinkNcp::requestActiveEndpoints(NodeId target) {
    telink::ZigbeeDiscoveryActiveEpReq req{
        .dstAddr = target,
        .nwkAddrOfInterest = target,
    };

    hci_->send(req);
}

void TelinkNcp::requestSimpleDescriptor(NodeId target, uint8_t endpoint) {
    telink::ZigbeeDiscoverySimpleDescReq req{
        .dstAddr = target,
        .nwkAddrOfInterest = target,
        .endpoint = endpoint,
    };

    hci_->send(req);
}

void TelinkNcp::requestLeave(NodeId target, EUI64 deviceAddress) {
    Y_UNUSED(target);
    Y_UNUSED(deviceAddress);

    YIO_LOG_WARN("Not implemented");
}

void TelinkNcp::sendBindRequest(
    NodeId target,
    const EUI64& source,
    uint8_t sourceEndpoint,
    uint16_t clusterId,
    uint8_t destinationEndpoint)
{
    Y_UNUSED(target);
    Y_UNUSED(source);
    Y_UNUSED(sourceEndpoint);
    Y_UNUSED(clusterId);
    Y_UNUSED(destinationEndpoint);

    YIO_LOG_WARN("Not implemented");
}

Json::Value TelinkNcp::collectMetrics() {
    return Json::Value{Json::objectValue};
}

void TelinkNcp::setState(NcpState state) {
    if (state_ == state) {
        return;
    }

    state_ = state;

    if (auto listener = listener_.lock()) {
        listener->onStateChanged(state_);
    }
}
