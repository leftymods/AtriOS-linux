#pragma once

#include "ezsp_listener.h"

#include <yandex_io/libs/threading/blocking_queue.h>

#include <contrib/libs/libev/ev++.h>

#include <future>
#include <span>
#include <string>
#include <thread>

extern "C" {

#include PLATFORM_HEADER
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/stack/include/ember-types.h>
#include <smart_devices/third_party/gecko_sdk/protocol/zigbee/app/util/ezsp/ezsp-protocol.h>
}

namespace zigbee {

    class EzspException: public std::runtime_error {
    public:
        EzspException(EmberStatus status, const std::string& action);

        EmberStatus getStatus() const;

    private:
        EmberStatus status_;
    };

    class Ezsp {
    public:
        static Ezsp& getInstance();

        void setListener(std::weak_ptr<IEzspListener> listener);
        const std::weak_ptr<IEzspListener>& getListener() const;

        void start(const std::string& port);
        void stop();

    public:
        // Configuration Frames

        uint16_t getConfigurationValue(EzspConfigId configId);
        void setConfigurationValue(EzspConfigId configId, uint16_t value);

        void setPolicy(EzspPolicyId policyId, EzspDecisionId decisionId);
        EzspDecisionId getPolicy(EzspPolicyId policyId);

        void setValue(EzspValueId valueId, std::span<uint8_t> value);

    public:
        // Utilities Frames

        void nop();

        std::vector<uint8_t> echo(const std::vector<uint8_t>& data);

        using Counters = std::array<uint16_t, EMBER_COUNTER_TYPE_COUNT>;

        Counters readAndClearCounters();
        Counters readCounters();

        std::pair<uint16_t, uint16_t> getXncpInfo();

        void getEui64(EmberEUI64 eui64);

    public:
        // Networking Frames

        void setManufacturerCode(uint16_t manufacturerCode);

        void networkInit(const EmberNetworkInitStruct& networkInitStruct);

        void formNetwork(const EmberNetworkParameters& parameters);

        void leaveNetwork();

        void permitJoining(uint8_t duration);

    public:
        // Messaging Frames

        uint8_t sendUnicast(
            EmberOutgoingMessageType type,
            EmberNodeId indexOrDestination,
            const EmberApsFrame& apsFrame,
            uint8_t messageTag,
            const std::vector<uint8_t>& message);

        uint8_t sendBroadcast(
            EmberNodeId destination,
            const EmberApsFrame& apsFrame,
            uint8_t radius,
            uint8_t messageTag,
            const std::vector<uint8_t>& message);

    public:
        // ZDO

        void sendActiveEndpointsRequest(
            EmberNodeId target,
            EmberApsOption options);

        void sendSimpleDescriptorRequest(
            EmberNodeId target,
            uint8_t targetEndpoint,
            EmberApsOption options);

        void sendLeaveRequest(
            EmberNodeId target,
            const EmberEUI64 deviceAddress,
            EmberLeaveRequestFlags leaveRequestFlags,
            EmberApsOption options);

        void sendZigDevRequest(
            EmberNodeId destination,
            uint16_t clusterId,
            EmberApsOption options,
            const std::vector<uint8_t>& payload);

    public:
        // Security Frames

        void setInitialSecurityState(const EmberInitialSecurityState& state);
        void setExtendedSecurityBitmask(EmberExtendedSecurityBitmask mask);

        EmberKeyStruct getKey(EmberKeyType keyType);

        void addTransientLinkKey(const EmberEUI64 partner, const EmberKeyData& transientKey);
        void clearTransientLinkKeys();

    public:
        // Mfglib Frames

        void mfglibStart();
        void mfglibEnd();

        void mfglibSetChannel(uint8_t channel);
        uint8_t mfglibGetChannel();

        void mfglibSetPower(int8_t power);
        int8_t mfglibGetPower();

        void mfglibSendPacket(const std::vector<uint8_t>& payload);

    public:
        // Bootloader Frames

        void launchStandaloneBootloader(uint8_t mode);

    private:
        Ezsp();

        void ezspLoop();

        void ezspCallback();
        void queueCallback();
        void stopCallback();

        template <auto Func, class... Args, std::enable_if_t<!std::is_void_v<decltype(Func(std::declval<Args>()...))>, int> = 0>
        auto ezspRun(Args... args) {
            if (std::this_thread::get_id() != ezspThread_.get_id()) {
                std::promise<decltype(Func(args...))> promise;
                queue_.push([&]() {
                    promise.set_value(Func(args...));
                });
                queueWatcher_.send();
                return promise.get_future().get();
            } else {
                return Func(args...);
            }
        }

        template <auto Func, class... Args, std::enable_if_t<std::is_void_v<decltype(Func(std::declval<Args>()...))>, int> = 0>
        void ezspRun(Args... args) {
            if (std::this_thread::get_id() != ezspThread_.get_id()) {
                std::promise<void> promise;
                queue_.push([&]() {
                    Func(args...);
                    promise.set_value();
                });
                queueWatcher_.send();
                promise.get_future().get();
            } else {
                Func(args...);
            }
        }

    private:
        std::thread ezspThread_;

        ev::dynamic_loop loop_;
        ev::io ezspWatcher_;
        ev::async queueWatcher_;
        ev::async stopWatcher_;

        quasar::BlockingQueue<std::function<void()>> queue_;

        std::weak_ptr<IEzspListener> listener_;
    };

} // namespace zigbee
