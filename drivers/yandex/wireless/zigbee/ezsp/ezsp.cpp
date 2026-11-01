#include "ezsp.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

extern "C" {

#include PLATFORM_HEADER
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/stack/include/error.h>
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/stack/include/ember-types.h>
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/ezsp/ezsp-protocol.h>
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/ezsp/ezsp.h>
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/zigbee-framework/zigbee-device-common.h>
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/app/ezsp-host/ash/ash-host.h>
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/app/ezsp-host/ezsp-host-io.h>
}

using namespace zigbee;

namespace {

    std::string formatErrorMessage(uint8_t status, const std::string& action) {
        std::ostringstream ss;
        ss << action << ": " << quasar::Hex(status);
        return ss.str();
    }

    inline void checkStatus(EmberStatus status, const std::string& action) {
        if (status != EMBER_SUCCESS) {
            throw EzspException(status, action);
        }
    }

} // namespace

EzspException::EzspException(EmberStatus status, const std::string& action)
    : std::runtime_error(formatErrorMessage(status, action))
    , status_(status)
{
}

EmberStatus EzspException::getStatus() const {
    return status_;
}

Ezsp& Ezsp::getInstance() {
    static Ezsp instance;
    return instance;
}

Ezsp::Ezsp()
    : ezspWatcher_(loop_)
    , queueWatcher_(loop_)
    , stopWatcher_(loop_)
    , queue_(100)
{
    ezspWatcher_.set<Ezsp, &Ezsp::ezspCallback>(this);
    queueWatcher_.set<Ezsp, &Ezsp::queueCallback>(this);
    stopWatcher_.set<Ezsp, &Ezsp::stopCallback>(this);
}

void Ezsp::setListener(std::weak_ptr<IEzspListener> listener) {
    listener_ = std::move(listener);
}

const std::weak_ptr<IEzspListener>& Ezsp::getListener() const {
    return listener_;
}

void Ezsp::start(const std::string& port) {
    strncpy(ashHostConfig.serialPort, port.c_str(), ASH_PORT_LEN);

    EzspStatus status = ::ezspInit();

    if (status != EZSP_SUCCESS) {
        throw EzspException(status, "ezspInit");
    }

    YIO_LOG_INFO("EZSP started");

    uint8_t ncpStackType;
    uint16_t ncpStackVersion;
    uint8_t ezspProtocolVersion = ezspVersion(EZSP_PROTOCOL_VERSION, &ncpStackType, &ncpStackVersion);

    YIO_LOG_DEBUG("EZSP protocol version: " << quasar::Hex(ezspProtocolVersion));
    YIO_LOG_DEBUG("NCP stack type: " << quasar::Hex(ncpStackType));
    YIO_LOG_DEBUG("NCP stack version: " << quasar::Hex(ncpStackVersion));

    const int ezspFd = ::ezspSerialGetFd();
    YIO_LOG_DEBUG("EZSP FD: " << ezspFd);

    ezspWatcher_.start(ezspFd, ev::READ);
    queueWatcher_.start();
    stopWatcher_.start();

    ezspThread_ = std::thread(&Ezsp::ezspLoop, this);
}

void Ezsp::stop() {
    stopWatcher_.send();

    if (ezspThread_.joinable()) {
        ezspThread_.join();

        YIO_LOG_INFO("EZSP stopped");
    }

    ezspWatcher_.stop();
    queueWatcher_.stop();
    stopWatcher_.stop();
}

void Ezsp::ezspLoop() {
    loop_.run();

    ::ezspClose();

    queue_.clear();
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void Ezsp::ezspCallback() {
    ::ezspTick();

    while (::ezspCallbackPending()) {
        ::ezspCallback();
    }
}

void Ezsp::queueCallback() {
    std::function<void()> task;
    while (queue_.tryPop(task) && task) {
        task();
    }
}

void Ezsp::stopCallback() {
    loop_.break_loop();
}

uint16_t Ezsp::getConfigurationValue(EzspConfigId configId) {
    uint16_t value;
    checkStatus(ezspRun<::ezspGetConfigurationValue>(configId, &value), "getConfigurationValue");
    return value;
}

void Ezsp::setConfigurationValue(EzspConfigId configId, uint16_t value) {
    checkStatus(ezspRun<::ezspSetConfigurationValue>(configId, value), "setConfigurationValue");
}

void Ezsp::setPolicy(EzspPolicyId policyId, EzspDecisionId decisionId) {
    checkStatus(ezspRun<::ezspSetPolicy>(policyId, decisionId), "setPolicy");
}

EzspDecisionId Ezsp::getPolicy(EzspPolicyId policyId) {
    EzspDecisionId decisionId;
    checkStatus(ezspRun<::ezspGetPolicy>(policyId, &decisionId), "setPolicy");
    return decisionId;
}

void Ezsp::setValue(EzspValueId valueId, std::span<uint8_t> value) {
    checkStatus(ezspRun<::ezspSetValue>(valueId, value.size(), value.data()), "setValue");
}

void Ezsp::nop() {
    ezspRun<::ezspNop>();
}

std::vector<uint8_t> Ezsp::echo(const std::vector<uint8_t>& data) {
    uint8_t echo[128];

    if (data.size() > sizeof(echo)) {
        throw std::runtime_error("Data is too large");
    }

    uint8_t echoLength = ezspRun<::ezspEcho>(data.size(), const_cast<uint8_t*>(data.data()), echo);

    return std::vector<uint8_t>{echo, echo + echoLength};
}

Ezsp::Counters Ezsp::readAndClearCounters() {
    Ezsp::Counters counters;

    ezspRun<::ezspReadAndClearCounters>(counters.data());

    return counters;
}

Ezsp::Counters Ezsp::readCounters() {
    Ezsp::Counters counters;

    ezspRun<::ezspReadCounters>(counters.data());

    return counters;
}

std::pair<uint16_t, uint16_t> Ezsp::getXncpInfo() {
    uint16_t manufacturerId;
    uint16_t versionNumber;

    checkStatus(ezspRun<::ezspGetXncpInfo>(&manufacturerId, &versionNumber), "getXncpInfo");

    return std::make_pair(manufacturerId, versionNumber);
}

void Ezsp::getEui64(EmberEUI64 eui64) {
    ezspRun<::ezspGetEui64>(eui64);
}

void Ezsp::setManufacturerCode(uint16_t manufacturerCode) {
    ezspRun<::ezspSetManufacturerCode>(manufacturerCode);
}

void Ezsp::networkInit(const EmberNetworkInitStruct& networkInitStruct) {
    checkStatus(ezspRun<::ezspNetworkInit>(const_cast<EmberNetworkInitStruct*>(&networkInitStruct)), "networkInit");
}

void Ezsp::formNetwork(const EmberNetworkParameters& parameters) {
    checkStatus(ezspRun<::ezspFormNetwork>(const_cast<EmberNetworkParameters*>(&parameters)), "formNetwork");
}

void Ezsp::leaveNetwork() {
    checkStatus(ezspRun<::ezspLeaveNetwork>(), "leaveNetwork");
}

void Ezsp::permitJoining(uint8_t duration) {
    checkStatus(ezspRun<::ezspPermitJoining>(duration), "permitJoining");
}

uint8_t Ezsp::sendUnicast(
    EmberOutgoingMessageType type,
    EmberNodeId indexOrDestination,
    const EmberApsFrame& apsFrame,
    uint8_t messageTag,
    const std::vector<uint8_t>& message) {
    uint8_t sequence;

    checkStatus(ezspRun<::ezspSendUnicast>(
                    type,
                    indexOrDestination,
                    const_cast<EmberApsFrame*>(&apsFrame),
                    messageTag,
                    message.size(),
                    const_cast<uint8_t*>(message.data()),
                    &sequence), "sendUnicast");

    return sequence;
}

uint8_t Ezsp::sendBroadcast(
    EmberNodeId destination,
    const EmberApsFrame& apsFrame,
    uint8_t radius,
    uint8_t messageTag,
    const std::vector<uint8_t>& message) {
    uint8_t sequence;

    checkStatus(ezspRun<::ezspSendBroadcast>(
                    destination,
                    const_cast<EmberApsFrame*>(&apsFrame),
                    radius,
                    messageTag,
                    message.size(),
                    const_cast<uint8_t*>(message.data()),
                    &sequence), "sendUnicast");

    return sequence;
}

void Ezsp::sendActiveEndpointsRequest(
    EmberNodeId target,
    EmberApsOption options) {
    checkStatus(ezspRun<::emberSendZigDevRequestTarget>(target, ACTIVE_ENDPOINTS_REQUEST, options), "sendActiveEndpointsRequest");
}

void Ezsp::sendSimpleDescriptorRequest(
    EmberNodeId target,
    uint8_t targetEndpoint,
    EmberApsOption options) {
    checkStatus(ezspRun<::emberSimpleDescriptorRequest>(target, targetEndpoint, options), "sendSimpleDescriptorRequest");
}

void Ezsp::sendLeaveRequest(
    EmberNodeId target,
    const EmberEUI64 deviceAddress,
    EmberLeaveRequestFlags leaveRequestFlags,
    EmberApsOption options) {
    checkStatus(ezspRun<::emberLeaveRequest>(target, const_cast<uint8_t*>(deviceAddress), leaveRequestFlags, options), "sendLeaveRequest");
}

void Ezsp::sendZigDevRequest(
    EmberNodeId destination,
    uint16_t clusterId,
    EmberApsOption options,
    const std::vector<uint8_t>& payload)
{
    std::vector<uint8_t> contents(ZDO_MESSAGE_OVERHEAD + payload.size());
    std::copy(payload.begin(), payload.end(), contents.begin() + ZDO_MESSAGE_OVERHEAD);

    checkStatus(ezspRun<::emberSendZigDevRequest>(destination, clusterId, options, contents.data(), contents.size()), "sendZigDevRequest");
}

void Ezsp::setInitialSecurityState(const EmberInitialSecurityState& state) {
    checkStatus(ezspRun<::ezspSetInitialSecurityState>(const_cast<EmberInitialSecurityState*>(&state)), "setInitialSecurityState");
}

void Ezsp::setExtendedSecurityBitmask(EmberExtendedSecurityBitmask mask) {
    checkStatus(ezspRun<::emberSetExtendedSecurityBitmask>(mask), "setExtendedSecurityBitmask");
}

EmberKeyStruct Ezsp::getKey(EmberKeyType keyType) {
    EmberKeyStruct keyStruct;

    checkStatus(ezspRun<::ezspGetKey>(keyType, &keyStruct), "getKey");

    return keyStruct;
}

void Ezsp::addTransientLinkKey(const EmberEUI64 partner, const EmberKeyData& transientKey) {
    checkStatus(ezspRun<::ezspAddTransientLinkKey>(const_cast<uint8_t*>(partner), const_cast<EmberKeyData*>(&transientKey)), "addTransientLinkKey");
}

void Ezsp::clearTransientLinkKeys() {
    ezspRun<::ezspClearTransientLinkKeys>();
}

void Ezsp::mfglibStart() {
    checkStatus(ezspRun<::ezspMfglibStart>(true), "mfglibStart");
}

void Ezsp::mfglibEnd() {
    checkStatus(ezspRun<::mfglibEnd>(), "mfglibEnd");
}

void Ezsp::mfglibSetChannel(uint8_t channel) {
    checkStatus(ezspRun<::mfglibSetChannel>(channel), "mfglibSetChannel");
}

uint8_t Ezsp::mfglibGetChannel() {
    return ezspRun<::mfglibGetChannel>();
}

void Ezsp::mfglibSetPower(int8_t power) {
    checkStatus(ezspRun<::mfglibSetPower>(EMBER_TX_POWER_MODE_DEFAULT, power), "mfglibSetPower");
}

int8_t Ezsp::mfglibGetPower() {
    return ezspRun<::mfglibGetPower>();
}

void Ezsp::mfglibSendPacket(const std::vector<uint8_t>& payload) {
    if (payload.empty() || payload.size() > 120) {
        throw std::runtime_error("Invalid payload length");
    }

    const uint8_t packetLength = payload.size() + 2;

    std::vector<uint8_t> packetContents(packetLength);

    std::copy(payload.begin(), payload.end(), packetContents.begin());

    auto status = ezspRun<::mfglibSendPacket>(
        packetLength,
        packetContents.data());

    checkStatus(status, "mfglibSendPacket");
}

void Ezsp::launchStandaloneBootloader(uint8_t mode) {
    checkStatus(ezspRun<::ezspLaunchStandaloneBootloader>(mode), "launchStandaloneBootloader");
}
