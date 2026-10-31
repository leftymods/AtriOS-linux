#pragma once

#include <smart_devices/libs/zigbee/ncp/i_ncp.h>

#include <yandex_io/libs/threading/i_callback_queue.h>

namespace telink {

    class Hci;

} // namespace telink

namespace {

    class TelinkNcpHandler;

} // namespace

namespace zigbee {

    class TelinkNcp: public INcp {
    public:
        TelinkNcp(const std::string& port);
        ~TelinkNcp();

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

    private:
        void setState(NcpState state);

    private:
        const std::string port_;
        const std::unique_ptr<quasar::ICallbackQueue> callbackQueue_;
        const std::shared_ptr<TelinkNcpHandler> handler_;

        NcpState state_;

        std::weak_ptr<INcpListener> listener_;

        std::unique_ptr<telink::Hci> hci_;

        EUI64 eui64_ = {};

        friend class ::TelinkNcpHandler;
    };

} // namespace zigbee
