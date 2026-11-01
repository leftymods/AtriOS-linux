#pragma once

#include <smart_devices/libs/zigbee/ezsp/ezsp.h>
#include <smart_devices/libs/zigbee/ncp/i_ncp.h>
#include <smart_devices/libs/zigbee/ncp/ncp_configuration.h>

#include <yandex_io/libs/threading/i_callback_queue.h>

namespace {

    class EzspListener;

} // namespace

namespace zigbee {

    class SilabsNcp: public INcp {
    public:
        SilabsNcp(const std::string& port, NcpConfiguration configuration = NcpConfiguration());
        SilabsNcp(const std::string& port, NcpConfiguration configuration, std::unique_ptr<quasar::ICallbackQueue> callbackQueue);
        ~SilabsNcp();

        void launchBootloader() override;

        FirmwareVersion getVersion() override;

        const EUI64& getEui64() override;

        void formNetwork(const NetworkParameters& parameters) override;

        void leaveNetwork() override;

        void openNetwork(uint8_t duration) override;

        void closeNetwork() override;

        void sendMessage(
            NodeId destination,
            uint16_t profileId,
            uint16_t clusterId,
            uint8_t sourceEndpoint,
            uint8_t destinationEndpoint,
            const std::vector<uint8_t>& message) override;

        void sendZclCommand(
            NodeId destination,
            uint16_t profileId,
            uint16_t clusterId,
            uint8_t sourceEndpoint,
            uint8_t destinationEndpoint,
            ZclFrameType frameType,
            std::optional<uint16_t> manufacturerCode,
            ZclDirection direction,
            uint8_t commandId,
            const std::vector<uint8_t>& payload) override;

        void requestActiveEndpoints(NodeId target) override;

        void requestSimpleDescriptor(NodeId target, uint8_t endpoint) override;

        void requestLeave(NodeId target, EUI64 deviceAddress) override;

        void sendBindRequest(NodeId target, const EUI64& source, uint8_t sourceEndpoint, uint16_t clusterId, uint8_t destinationEndpoint) override;

        void init() override;

        void setListener(std::weak_ptr<INcpListener> listener) override;

        Json::Value collectMetrics() override;

    protected:
        void broadcastPermitJoiningRequest(uint8_t duration);

    private:
        void setState(NcpState state);

        void onError();

        void reinitialize();

        void onCounterRollover(size_t counter);
        void onMessageSent(uint8_t type);

        void sendUnicast(std::function<void()> f, bool isRetry = false);
        void retryUnicast(std::optional<uint32_t> token);
        size_t getUnicastQueueSize();

        uint32_t getNwkFrameCounter();
        uint32_t getApsFrameCounter();

        std::map<std::string, size_t> collectCounters();

    protected:
        Ezsp& ezsp_;
        const std::string port_;
        const NcpConfiguration configuration_;
        const std::shared_ptr<EzspListener> ezspListener_;
        const std::unique_ptr<quasar::ICallbackQueue> callbackQueue_;

        std::weak_ptr<INcpListener> listener_;

        EUI64 eui64_ = {};
        uint8_t zclTransactionSequenceNumber_ = 0;

        std::recursive_mutex mutex_;
        NcpState state_;
        std::vector<size_t> counters_;
        std::queue<std::function<void()>> unicastQueue_;
        uint32_t retryToken_ = 0;

        friend class ::EzspListener;
    };

} // namespace zigbee
