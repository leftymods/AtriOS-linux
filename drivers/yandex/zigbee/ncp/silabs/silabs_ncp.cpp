#include "silabs_ncp.h"

#include <smart_devices/libs/zigbee/ezsp/ezsp.h>
#include <smart_devices/libs/zigbee/zcl/zcl_stream.h>

#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <util/generic/scope.h>

YIO_DEFINE_LOG_MODULE("zigbee");

using namespace zigbee;

namespace {

    constexpr EmberEUI64 WILDCARD_EUI64 = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    constexpr EmberKeyData ZIGBEE_ALLIANCE_09 = {
        {0x5A, 0x69, 0x67, 0x42, 0x65, 0x65, 0x41, 0x6C,
         0x6C, 0x69, 0x61, 0x6E, 0x63, 0x65, 0x30, 0x39}};

    constexpr std::string_view COUNTER_NAMES[] = {
        "MacRxBcast",
        "MacTxBcast",
        "MacRxUcast",
        "MacTxUcast",
        "MacTxUcastRetry",
        "MacTxUcastFail",
        "APSRxBcast",
        "APSTxBcast",
        "APSRxUcast",
        "APSTxUcastSuccess",
        "APSTxUcastRetry",
        "APSTxUcastFail",
        "RouteDiscInitiated",
        "NeighborAdded",
        "NeighborRemoved",
        "NeighborStale",
        "JoinIndication",
        "ChildMoved",
        "ASHOverflow",
        "ASHFrameError",
        "ASHOverrunError",
        "NWKFCFailure",
        "APSFCFailure",
        "ASHXOff",
        "APSUnauthorizedKey",
        "NWKDecryptFailures",
        "APSDecryptFailures",
        "PacketBufferAllocateFailures",
        "RelayedUcast",
        "PhyToMACQueueLimitReached",
        "PacketValidateDropCount",
        "NWKRetryOverflow",
        "CCAFailures",
        "BroadcastTableFull",
        "PTALoPriReq",
        "PTAHiPriReq",
        "PTALoPriDenied",
        "PTAHiPriDenied",
        "PTALoPriTxAbrt",
        "PTAHiPriTxAbrt",
        "AddressConflictSent"};

    constexpr auto UNICAST_RETRY_TIMEOUT = std::chrono::seconds(5);

    class EzspListener: public IEzspListener {
    public:
        EzspListener(SilabsNcp& ncp)
            : ncp_(ncp)
        {
        }

        void onCounterRollover(EmberCounterType type) override {
            ncp_.callbackQueue_->add([this, type]() {
                ncp_.onCounterRollover(type);
            });
        }

        void onStackStatus(EzspStatus status) override {
            ncp_.callbackQueue_->add([this, status]() {
                switch (status) {
                    case EMBER_NETWORK_OPENED:
                        if (auto listener = ncp_.listener_.lock()) {
                            listener->onNetworkOpen(true);
                        }
                        break;
                    case EMBER_NETWORK_CLOSED:
                        if (auto listener = ncp_.listener_.lock()) {
                            listener->onNetworkOpen(false);
                        }
                        break;
                }
            });
        }

        void onError(EzspStatus status) override {
            if (status == EZSP_ERROR_OVERFLOW) {
                return;
            }

            ncp_.callbackQueue_->add([this]() {
                ncp_.onError();
            });
        };

        void onMessageSent(
            EmberOutgoingMessageType type,
            uint16_t indexOrDestination,
            const EmberApsFrame& apsFrame,
            uint8_t messageTag,
            EmberStatus status,
            const std::vector<uint8_t>& message) override {
            Y_UNUSED(indexOrDestination);
            Y_UNUSED(apsFrame);
            Y_UNUSED(messageTag);
            Y_UNUSED(status);
            Y_UNUSED(message);
            ncp_.callbackQueue_->add([this, type]() {
                ncp_.onMessageSent(type);
            });
        }

        void onIncomingMessage(
            EmberIncomingMessageType type,
            const EmberApsFrame& apsFrame,
            uint8_t lastHopLqi,
            int8_t lastHopRssi,
            EmberNodeId sender,
            uint8_t bindingIndex,
            uint8_t addressIndex,
            const std::vector<uint8_t>& message) override {
            (void)type;
            (void)lastHopLqi;
            (void)lastHopRssi;
            (void)bindingIndex;
            (void)addressIndex;

            ncp_.callbackQueue_->add([this, sender, apsFrame, message]() {
                if (auto listener = ncp_.listener_.lock()) {
                    if (listener->onRawMessage(sender, apsFrame.profileId, apsFrame.clusterId, message)) {
                        return;
                    }
                }

                if (apsFrame.profileId == EMBER_ZDO_PROFILE_ID) {
                    processZdoMessage(sender, apsFrame.clusterId, message);
                } else {
                    processZclMessage(sender, apsFrame.sourceEndpoint, apsFrame.profileId, apsFrame.clusterId, message);
                }
            });
        }

        void onMfglibRx(uint8_t /* lqi */, int8_t /* rssi */, const std::vector<uint8_t>& /* payload */) override{};

    private:
        void processZdoMessage(EmberNodeId nodeId, uint16_t clusterId, const std::vector<uint8_t>& message) {
            YIO_LOG_DEBUG("ZDO message:"
                          << " nodeId " << quasar::Hex(nodeId)
                          << " clusterId " << quasar::Hex(clusterId)
                          << " data " << quasar::Hex(message));

            switch (clusterId) {
                case END_DEVICE_ANNOUNCE: {
                    if (message.size() != 12) {
                        YIO_LOG_WARN("Malformed END_DEVICE_ANNOUNCE");
                        break;
                    }

                    NodeId nodeId = message[1] + (message[2] << 8);

                    EUI64 eui64;
                    std::copy(&message[3], &message[3 + eui64.size()], eui64.begin());

                    uint8_t capability = message[11];

                    if (auto listener = ncp_.listener_.lock()) {
                        listener->onDeviceAnnounce(nodeId, eui64, capability);
                    }

                    break;
                }
                case ACTIVE_ENDPOINTS_RESPONSE: {
                    if (message.size() < 5) {
                        YIO_LOG_WARN("Malformed ACTIVE_ENDPOINTS_RESPONSE");
                        break;
                    }

                    EmberZdoStatus status = message[1];
                    NodeId nodeId = message[2] + (message[3] << 8);
                    size_t activeEpCount = message[4];

                    if (status != EMBER_ZDP_SUCCESS) {
                        YIO_LOG_WARN("ACTIVE_ENDPOINTS_RESPONSE error: " << quasar::Hex(status));
                        break;
                    }

                    if (message.size() != 5 + activeEpCount) {
                        YIO_LOG_WARN("Malformed ACTIVE_ENDPOINTS_RESPONSE");
                        break;
                    }

                    std::vector<uint8_t> activeEpList{&message[5], &message[5 + activeEpCount]};

                    if (auto listener = ncp_.listener_.lock()) {
                        listener->onActiveEndpoints(nodeId, activeEpList);
                    }

                    break;
                }
                case SIMPLE_DESCRIPTOR_RESPONSE: {
                    if (message.size() < 5) {
                        YIO_LOG_WARN("Malformed SIMPLE_DESCRIPTOR_RESPONSE");
                        break;
                    }

                    EmberZdoStatus status = message[1];
                    NodeId nodeId = message[2] + (message[3] << 8);
                    size_t length = message[4];

                    if (status != EMBER_ZDP_SUCCESS) {
                        YIO_LOG_WARN("SIMPLE_DESCRIPTOR_RESPONSE error: " << quasar::Hex(status));
                        break;
                    }

                    if (message.size() != 5 + length) {
                        break;
                    }

                    size_t offset = 5;

                    SimpleDescriptor descriptor;
                    descriptor.endpoint = message[offset];
                    descriptor.profileId = message[offset + 1] + (message[offset + 2] << 8);
                    descriptor.deviceId = message[offset + 3] + (message[offset + 4] << 8);
                    descriptor.deviceVersion = message[offset + 5];

                    offset += 6;

                    size_t inputClustersCount = message[offset++];
                    if (message.size() < offset + 2 * inputClustersCount) {
                        YIO_LOG_WARN("Malformed SIMPLE_DESCRIPTOR_RESPONSE");
                        break;
                    }

                    descriptor.inputClusters = std::vector<uint16_t>{
                        (uint16_t*)&message[offset],
                        (uint16_t*)&message[offset + 2 * inputClustersCount]};

                    offset += 2 * inputClustersCount;

                    size_t outputClustersCount = message[offset++];
                    if (message.size() < offset + 2 * outputClustersCount) {
                        YIO_LOG_WARN("Malformed SIMPLE_DESCRIPTOR_RESPONSE");
                        break;
                    }

                    descriptor.outputClusters = std::vector<uint16_t>{
                        (uint16_t*)&message[offset],
                        (uint16_t*)&message[offset + 2 * outputClustersCount]};

                    if (auto listener = ncp_.listener_.lock()) {
                        listener->onSimpleDescriptor(nodeId, descriptor);
                    }

                    break;
                }
            }
        }

        void processZclMessage(EmberNodeId nodeId, uint8_t endpoint, uint16_t profileId, uint16_t clusterId, const std::vector<uint8_t>& message) {
            try {
                ZclStream zclStream(message);

                ZclHeader zclHeader;
                zclHeader.frameControl = zclStream.readUint8();
                if (zclHeader.isManufacturerSpecific()) {
                    zclHeader.manufacturerCode = zclStream.readUint16();
                } else {
                    zclHeader.manufacturerCode = 0;
                }
                zclHeader.transactionSequenceNumber = zclStream.readUint8();
                zclHeader.commandId = zclStream.readUint8();
                auto zclPayload = zclStream.getRemainingBuffer();

                if (auto listener = ncp_.listener_.lock()) {
                    listener->onZclMessage(nodeId, endpoint, profileId, clusterId, zclHeader, std::move(zclPayload));
                }
            } catch (const std::exception& e) {
                YIO_LOG_WARN("EzspListener.FailedToParseZclMessage: " << e.what());
            }
        }

    private:
        SilabsNcp& ncp_;
    };

} // namespace

SilabsNcp::SilabsNcp(const std::string& port, NcpConfiguration configuration)
    : SilabsNcp(port, configuration, std::make_unique<quasar::NamedCallbackQueue>("SilabsNCP"))
{
}

SilabsNcp::SilabsNcp(const std::string& port, NcpConfiguration configuration, std::unique_ptr<quasar::ICallbackQueue> callbackQueue)
    : ezsp_(Ezsp::getInstance())
    , port_(port)
    , configuration_(configuration)
    , ezspListener_(std::make_shared<EzspListener>(*this))
    , callbackQueue_(std::move(callbackQueue))
    , state_(NcpState::Uninitialized)
    , counters_(std::size(COUNTER_NAMES))
{
    static_assert(std::size(COUNTER_NAMES) == std::tuple_size<decltype(ezsp_.readAndClearCounters())>::value);

    ezsp_.setListener(ezspListener_);
}

SilabsNcp::~SilabsNcp() {
    ezsp_.stop();
}

void SilabsNcp::init() {
    ezsp_.stop();

    setState(NcpState::Uninitialized);

    ezsp_.start(port_);

    ezsp_.setConfigurationValue(EZSP_CONFIG_STACK_PROFILE, EMBER_STACK_PROFILE_ZIGBEE_PRO);
    ezsp_.setConfigurationValue(EZSP_CONFIG_SECURITY_LEVEL, EMBER_SECURITY_LEVEL_Z3);

    if (configuration_.beaconJitterDuration) {
        YIO_LOG_INFO("Setting beacon jitter duration to " << static_cast<uint32_t>(*configuration_.beaconJitterDuration));
        ezsp_.setConfigurationValue(EZSP_CONFIG_BEACON_JITTER_DURATION, *configuration_.beaconJitterDuration);
    }

    ezsp_.setPolicy(EZSP_TRUST_CENTER_POLICY, EZSP_DECISION_ALLOW_JOINS | EZSP_DECISION_ALLOW_UNSECURED_REJOINS);
    ezsp_.setPolicy(EZSP_TC_KEY_REQUEST_POLICY, EZSP_ALLOW_TC_KEY_REQUESTS_AND_SEND_CURRENT_KEY);
    ezsp_.setPolicy(EZSP_POLL_HANDLER_POLICY, EZSP_POLL_HANDLER_CALLBACK);

    if (configuration_.manufacturerCode) {
        YIO_LOG_INFO("Setting manufacturer code to " << quasar::Hex(*configuration_.manufacturerCode));
        ezsp_.setManufacturerCode(*configuration_.manufacturerCode);
    }

    ezsp_.getEui64(eui64_.data());
    YIO_LOG_INFO("EUI64 " << quasar::Hex(eui64_));

    setState(NcpState::Initialized);
}

void SilabsNcp::setListener(std::weak_ptr<INcpListener> listener) {
    listener_ = listener;
}

void SilabsNcp::launchBootloader() {
    ezsp_.launchStandaloneBootloader(1);
}

FirmwareVersion SilabsNcp::getVersion() {
    const auto version = ezsp_.getXncpInfo();
    return FirmwareVersion{
        .xncpManufacturerId = version.first,
        .xncpVersion = version.second};
}

const EUI64& SilabsNcp::getEui64() {
    return eui64_;
}

uint32_t SilabsNcp::getNwkFrameCounter() {
    const auto key = ezsp_.getKey(EMBER_CURRENT_NETWORK_KEY);
    return key.outgoingFrameCounter;
}

uint32_t SilabsNcp::getApsFrameCounter() {
    const auto key = ezsp_.getKey(EMBER_TRUST_CENTER_LINK_KEY);
    return key.outgoingFrameCounter;
}

std::map<std::string, size_t> SilabsNcp::collectCounters() {
    const auto values = ezsp_.readAndClearCounters();

    std::scoped_lock lock(mutex_);

    std::map<std::string, size_t> result;

    for (size_t i = 0; i < counters_.size(); i++) {
        counters_[i] += values[i];

        result.emplace(COUNTER_NAMES[i], counters_[i]);
    }

    return result;
}

Json::Value SilabsNcp::collectMetrics() {
    Json::Value metrics;

    Json::Value& counters = metrics["counters"];

    const auto counterValues = collectCounters();

    for (const auto& [counter, value] : counterValues) {
        counters[counter] = value;
    }

    try {
        metrics["nwkFrameCounter"] = getNwkFrameCounter();
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("zigbee.NCP.GetNwkFrameCounterFailed", "Failed to get NWK frame counter: " << e.what());
    }

    try {
        metrics["apsFrameCounter"] = getApsFrameCounter();
    } catch (const std::exception& e) {
        YIO_LOG_ERROR_EVENT("zigbee.NCP.GetApsFrameCounterFailed", "Failed to get APS frame counter: " << e.what());
    }

    metrics["unicastQueueSize"] = getUnicastQueueSize();

    return metrics;
}

void SilabsNcp::formNetwork(const NetworkParameters& parameters) {
    EmberInitialSecurityState security = {};

    security.bitmask = EMBER_TRUST_CENTER_GLOBAL_LINK_KEY | EMBER_TRUST_CENTER_USES_HASHED_LINK_KEY | EMBER_HAVE_PRECONFIGURED_KEY | EMBER_HAVE_NETWORK_KEY | EMBER_REQUIRE_ENCRYPTED_KEY | EMBER_NO_FRAME_COUNTER_RESET;

    std::copy(parameters.trustCenterLinkKey.begin(), parameters.trustCenterLinkKey.end(), security.preconfiguredKey.contents);
    std::copy(parameters.networkKey.begin(), parameters.networkKey.end(), security.networkKey.contents);
    security.networkKeySequenceNumber = parameters.networkKeySequenceNumber;

    EmberExtendedSecurityBitmask extended = EMBER_NWK_LEAVE_REQUEST_NOT_ALLOWED;

    ezsp_.setInitialSecurityState(security);
    ezsp_.setExtendedSecurityBitmask(extended);

    struct {
        uint16_t panId;
        int8_t radioTxPower;
        uint8_t radioFreqChannel;
        uint8_t stackProfile;
        uint8_t nodeType;
        uint16_t zigbeeNodeId;
        uint8_t extendedPanId[8];
    } nodeData = {};

    nodeData.panId = parameters.panId;
    nodeData.radioTxPower = 10; // XXX
    nodeData.radioFreqChannel = parameters.channel;
    nodeData.stackProfile = EMBER_STACK_PROFILE_ZIGBEE_PRO;
    nodeData.nodeType = EMBER_COORDINATOR;
    std::copy(parameters.extendedPanId.begin(), parameters.extendedPanId.end(), nodeData.extendedPanId);

    ezsp_.setValue(EZSP_VALUE_TOKEN_STACK_NODE_DATA, {reinterpret_cast<uint8_t*>(&nodeData), sizeof(nodeData)});

    EmberNetworkInitStruct networkInitStruct = {};
    networkInitStruct.bitmask = EMBER_NETWORK_INIT_NO_OPTIONS;

    ezsp_.networkInit(networkInitStruct);
}

void SilabsNcp::leaveNetwork() {
    ezsp_.leaveNetwork();
}

void SilabsNcp::openNetwork(uint8_t duration) {
    ezsp_.addTransientLinkKey(WILDCARD_EUI64, ZIGBEE_ALLIANCE_09);

    ezsp_.permitJoining(duration);

    if (configuration_.broadcastPermitJoining) {
        broadcastPermitJoiningRequest(duration);
    }
}

void SilabsNcp::closeNetwork() {
    ezsp_.permitJoining(0);

    if (configuration_.broadcastPermitJoining) {
        broadcastPermitJoiningRequest(0);
    }

    ezsp_.clearTransientLinkKeys();
}

void SilabsNcp::sendMessage(
    NodeId destination,
    uint16_t profileId,
    uint16_t clusterId,
    uint8_t sourceEndpoint,
    uint8_t destinationEndpoint,
    const std::vector<uint8_t>& message) {
    EmberApsFrame apsFrame = {};
    apsFrame.profileId = profileId;
    apsFrame.clusterId = clusterId;
    apsFrame.sourceEndpoint = sourceEndpoint;
    apsFrame.destinationEndpoint = destinationEndpoint;
    apsFrame.options = EMBER_APS_OPTION_RETRY | EMBER_APS_OPTION_ENABLE_ROUTE_DISCOVERY | EMBER_APS_OPTION_ENABLE_ADDRESS_DISCOVERY;

    if (emberIsZigbeeBroadcastAddress(destination)) {
        ezsp_.sendBroadcast(
            destination,
            apsFrame,
            0, // radius
            0, // messageTag
            message);
    } else {
        sendUnicast([this, destination, apsFrame, message]() {
            ezsp_.sendUnicast(
                EMBER_OUTGOING_DIRECT,
                destination,
                apsFrame,
                0, // messageTag
                message);
        });
    }
}

void SilabsNcp::sendZclCommand(
    NodeId destination,
    uint16_t profileId,
    uint16_t clusterId,
    uint8_t sourceEndpoint,
    uint8_t destinationEndpoint,
    ZclFrameType frameType,
    std::optional<uint16_t> manufacturerCode,
    ZclDirection direction,
    uint8_t commandId,
    const std::vector<uint8_t>& payload) {
    std::array<uint8_t, 5> header;
    auto it = header.begin();

    *it++ = ((frameType == ZclFrameType::GLOBAL) ? 0b00 : 0b01) << 0 | (manufacturerCode ? 1 : 0) << 2 | ((direction == ZclDirection::CLIENT_TO_SERVER) ? 0 : 1) << 3;

    if (manufacturerCode) {
        *it++ = (*manufacturerCode) & 0xff;
        *it++ = (*manufacturerCode) >> 8;
    }

    *it++ = zclTransactionSequenceNumber_++;
    *it++ = commandId;

    std::vector<uint8_t> message{header.begin(), it};
    message.insert(message.end(), payload.begin(), payload.end());

    sendMessage(
        destination,
        profileId,
        clusterId,
        sourceEndpoint,
        destinationEndpoint,
        message);
}

void SilabsNcp::requestActiveEndpoints(NodeId target) {
    sendUnicast([this, target]() {
        ezsp_.sendActiveEndpointsRequest(
            target,
            EMBER_APS_OPTION_RETRY | EMBER_APS_OPTION_ENABLE_ROUTE_DISCOVERY | EMBER_APS_OPTION_ENABLE_ADDRESS_DISCOVERY);
    });
}

void SilabsNcp::requestSimpleDescriptor(NodeId target, uint8_t endpoint) {
    sendUnicast([this, target, endpoint]() {
        ezsp_.sendSimpleDescriptorRequest(
            target,
            endpoint,
            EMBER_APS_OPTION_RETRY | EMBER_APS_OPTION_ENABLE_ROUTE_DISCOVERY | EMBER_APS_OPTION_ENABLE_ADDRESS_DISCOVERY);
    });
}

void SilabsNcp::requestLeave(NodeId target, EUI64 deviceAddress) {
    sendUnicast([this, target, deviceAddress]() {
        ezsp_.sendLeaveRequest(
            target,
            deviceAddress.data(),
            EMBER_ZIGBEE_LEAVE_WITHOUT_REJOIN, EMBER_APS_OPTION_RETRY);
    });
}

void SilabsNcp::sendBindRequest(NodeId target, const EUI64& source, uint8_t sourceEndpoint, uint16_t clusterId, uint8_t destinationEndpoint) {
    EmberEUI64 destination;
    ezsp_.getEui64(destination);

    std::vector<uint8_t> payload(21);
    std::copy(source.begin(), source.end(), payload.begin());
    payload[8] = sourceEndpoint;
    payload[9] = clusterId & 0xff;
    payload[10] = clusterId >> 8;
    payload[11] = UNICAST_BINDING;
    std::copy(std::begin(destination), std::end(destination), payload.begin() + 12);
    payload[20] = destinationEndpoint;

    sendUnicast([this, target, payload = std::move(payload)]() {
        ezsp_.sendZigDevRequest(
            target,
            BIND_REQUEST,
            EMBER_APS_OPTION_RETRY | EMBER_APS_OPTION_ENABLE_ROUTE_DISCOVERY | EMBER_APS_OPTION_ENABLE_ADDRESS_DISCOVERY,
            payload);
    });
}

void SilabsNcp::broadcastPermitJoiningRequest(uint8_t duration) {
    const std::vector<uint8_t> payload = {duration, 1};

    ezsp_.sendZigDevRequest(
        EMBER_BROADCAST_ADDRESS,
        PERMIT_JOINING_REQUEST,
        0,
        payload);
}

void SilabsNcp::setState(NcpState state) {
    std::scoped_lock lock(mutex_);

    if (state_ == state) {
        return;
    }

    state_ = state;

    if (auto listener = listener_.lock()) {
        listener->onStateChanged(state_);
    }
}

void SilabsNcp::onError() {
    YIO_LOG_ERROR_EVENT("zigbee.NCP.Error", "NCP failed");

    {
        std::scoped_lock lock(mutex_);

        if (state_ != NcpState::Initialized) {
            return;
        }

        setState(NcpState::Failed);
    }

    reinitialize();
}

void SilabsNcp::reinitialize() {
    YIO_LOG_INFO("Reinitializing NCP");

    try {
        init();
    } catch (const std::exception& e) {
        YIO_LOG_WARN("NCP reinitialization failed: " << e.what());

        callbackQueue_->addDelayed([this]() {
            reinitialize();
        }, std::chrono::seconds(60));
    }
}

void SilabsNcp::onCounterRollover(size_t counter) {
    std::scoped_lock lock(mutex_);

    counters_[counter] += 0x10000;
}

void SilabsNcp::onMessageSent(uint8_t type) {
    if (type == EMBER_OUTGOING_DIRECT || type == EMBER_OUTGOING_VIA_ADDRESS_TABLE || type == EMBER_OUTGOING_VIA_BINDING) {
        retryUnicast(std::nullopt);
    }
}

void SilabsNcp::sendUnicast(std::function<void()> f, bool isRetry) {
    if (!f) {
        return;
    }

    std::scoped_lock lock(mutex_);

    if (!isRetry && !unicastQueue_.empty()) {
        YIO_LOG_WARN("Unicast queue is not empty, queueing unicast");
        unicastQueue_.push(std::move(f));
        return;
    }

    retryToken_++;

    Y_DEFER {
        if (!unicastQueue_.empty()) {
            callbackQueue_->addDelayed([this, token = retryToken_]() {
                retryUnicast(token);
            }, UNICAST_RETRY_TIMEOUT);
        }
    };

    try {
        f();
    } catch (const EzspException& e) {
        if (e.getStatus() == EMBER_MAX_MESSAGE_LIMIT_REACHED) {
            YIO_LOG_WARN("Message limit reached, queueing unicast");

            unicastQueue_.push(std::move(f));

            return;
        }

        throw;
    }
}

void SilabsNcp::retryUnicast(std::optional<uint32_t> token) {
    std::scoped_lock lock(mutex_);

    if (token && *token != retryToken_) {
        return;
    }

    if (!unicastQueue_.empty()) {
        YIO_LOG_INFO("Retrying queued unicast (" << (token ? "timeout" : "handler") << ")");

        auto unicast = std::move(unicastQueue_.front());
        unicastQueue_.pop();

        try {
            sendUnicast(std::move(unicast), true);
        } catch (const EzspException& e) {
            YIO_LOG_ERROR_EVENT("zigbee.NCP.RetryUnicastFailed", "Failed to retry unicast: " << e.what());
        }
    }

    YIO_LOG_DEBUG(unicastQueue_.size() << " queued unicasts");
}

size_t SilabsNcp::getUnicastQueueSize() {
    std::scoped_lock lock(mutex_);

    return unicastQueue_.size();
}
