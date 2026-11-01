#pragma once

#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/iot/i_iot_discovery.h>
#include <yandex_io/libs/iot/i_iot_discovery_provider.h>
#include <yandex_io/sdk/sdk_interface.h>
#include <yandex_io/sdk/interfaces/i_capability.h>
#include <yandex_io/sdk/interfaces/i_directive_handler.h>
#include <smart_devices/libs/iot/capability/interface/i_iot_capability_events.h>

#include <alice/megamind/protos/common/frame.pb.h>

namespace YandexIO {

    class IotCapability
        : public ICapability,
          public IDirectiveHandler,
          public std::enable_shared_from_this<IotCapability> {
        struct WifiInfo {
            std::string wifiSSID;
            std::string wifiPassword;
        };

        struct CurrentDiscoveryInfo {
            std::chrono::milliseconds timeout{std::chrono::minutes(1)};
            std::string deviceType;
            std::string ssid;
            std::string password;
        };

    public:
        IotCapability(std::shared_ptr<SDKInterface> sdk,
                      std::shared_ptr<IDevice> device,
                      std::weak_ptr<IIotCapabilityEvents> events,
                      std::shared_ptr<YandexIO::IIotDiscoveryProvider> discoveryProvider);

        // ICapability implementation
        //
        NAlice::TCapabilityHolder getState() const override;
        YandexIO::IDirectiveHandlerPtr getDirectiveHandler() override;

        void addListener(std::weak_ptr<ICapability::IListener> wlistener) override;
        void removeListener(std::weak_ptr<ICapability::IListener> wlistener) override;

        // IDirectiveHandler implementation
        //
        const std::string& getHandlerName() const override;
        const std::set<std::string>& getSupportedDirectiveNames() const override;
        void handleDirective(const std::shared_ptr<Directive>& directive) override;
        void cancelDirective(const std::shared_ptr<Directive>& directive) override;
        void prefetchDirective(const std::shared_ptr<Directive>& directive) override;

    private:
        void onStartDiscovery(const Json::Value& payload);
        void onStopDiscovery(const Json::Value& payload);
        void onDiscoveryCredentials(const Json::Value& payload);
        void installCurrentDiscoveryTimeout(std::chrono::milliseconds timeout);

        void transitionState(quasar::proto::IotState::State newState, quasar::proto::IotDiscoveryResult result);
        void ensureDiscoveryStopped(const quasar::proto::IotDiscoveryResult& reason);
        void ensureDiscoveryStopped(quasar::proto::IotDiscoveryResult::ResultCode resultCode);

        Json::Value createStartDiscoverySemanticFrame() const;
        Json::Value createFailDiscoverySemanticFrame(quasar::proto::IotDiscoveryResult result) const;

    private:
        static NAlice::TCapabilityHolder createIotDiscoveryCapability();
        static Json::Value createSemanticFrame(const NAlice::TSemanticFrameRequestData& data);
        static quasar::proto::IotDiscoveryResult::ResultCode parseDiscoveryResultCode(const std::string& code);

    private:
        NAlice::TCapabilityHolder state_;
        std::shared_ptr<IDevice> device_;
        std::shared_ptr<SDKInterface> sdk_;
        std::list<std::weak_ptr<ICapability::IListener>> wlisteners_;
        quasar::NamedCallbackQueue callbackQueue_;
        std::weak_ptr<IIotCapabilityEvents> events_;

        // old iot
        //
        std::shared_ptr<IIotDiscoveryProvider> discoveryProvider_;
        std::shared_ptr<IIotDiscovery> currentDiscovery_;
        std::optional<CurrentDiscoveryInfo> currentDiscoveryInfo_;
        quasar::proto::IotState::State iotState_ = quasar::proto::IotState::IDLE;
        std::shared_ptr<const YandexIO::LatencyData> waitLatencyPoint_;
        std::shared_ptr<const YandexIO::LatencyData> prepareLatencyPoint_;
        std::string wifiStoragePath_;
    };

} // namespace YandexIO
