#include "iot_capability.h"

#include <alice/megamind/protos/common/atm.pb.h>

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/base/directives.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/net/wifi_info.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/protobuf_utils/json.h>

#include <util/folder/path.h>

YIO_DEFINE_LOG_MODULE("iot_capability");

namespace YandexIO {

    IotCapability::IotCapability(
        std::shared_ptr<SDKInterface> sdk,
        std::shared_ptr<IDevice> device,
        std::weak_ptr<IIotCapabilityEvents> events,
        std::shared_ptr<YandexIO::IIotDiscoveryProvider> discoveryProvider)
        : state_(createIotDiscoveryCapability())
        , device_(std::move(device))
        , sdk_(std::move(sdk))
        , callbackQueue_("iot_capability_queue")
        , events_(std::move(events))
        , discoveryProvider_(std::move(discoveryProvider))
    {
        const auto firstRunConfig = device_->configuration()->getServiceConfig("firstrund");
        wifiStoragePath_ = quasar::getString(firstRunConfig, "wifiStoragePath");
    }

    NAlice::TCapabilityHolder IotCapability::getState() const {
        return state_;
    }

    IDirectiveHandlerPtr IotCapability::getDirectiveHandler() {
        return shared_from_this();
    }

    void IotCapability::addListener(std::weak_ptr<ICapability::IListener> wlistener)
    {
        callbackQueue_.add([this, wlistener]() {
            wlisteners_.push_back(wlistener);
        });
    }

    void IotCapability::removeListener(std::weak_ptr<ICapability::IListener> wlistener) {
        callbackQueue_.add([this, wlistener]() {
            const auto iter = std::find_if(wlisteners_.begin(), wlisteners_.end(),
                                           [wlistener](const auto& wp) {
                                               return !(wp.owner_before(wlistener) || wlistener.owner_before(wp));
                                           });
            if (iter != wlisteners_.end()) {
                wlisteners_.erase(iter);
            }
        });
    }

    const std::string& IotCapability::getHandlerName() const {
        static const std::string s_name = "IotCapabilityDirectiveHandler";
        return s_name;
    }

    const std::set<std::string>& IotCapability::getSupportedDirectiveNames() const {
        static const std::set<std::string> s_names = {
            quasar::Directives::IOT_DISCOVERY_START,
            quasar::Directives::IOT_DISCOVERY_STOP,
            quasar::Directives::IOT_DISCOVERY_CREDENTIALS};

        return s_names;
    }

    void IotCapability::handleDirective(const std::shared_ptr<Directive>& directive) {
        callbackQueue_.add([this, directive]() {
            YIO_LOG_INFO("handleDirective " << directive->format());

            if (directive->is(quasar::Directives::IOT_DISCOVERY_START)) {
                onStartDiscovery(directive->getData().payload);
            } else if (directive->is(quasar::Directives::IOT_DISCOVERY_STOP)) {
                onStopDiscovery(directive->getData().payload);
            } else if (directive->is(quasar::Directives::IOT_DISCOVERY_CREDENTIALS)) {
                onDiscoveryCredentials(directive->getData().payload);
            }
        });
    }

    void IotCapability::cancelDirective(const std::shared_ptr<Directive>& /* directive */) {
        // do nothing
    }

    void IotCapability::prefetchDirective(const std::shared_ptr<Directive>& /* directive */) {
        // do nothing
    }

    NAlice::TCapabilityHolder IotCapability::createIotDiscoveryCapability()
    {
        NAlice::TCapabilityHolder capability;
        return capability;
    }

    void IotCapability::onStartDiscovery(const Json::Value& payload) {
        ensureDiscoveryStopped(quasar::proto::IotDiscoveryResult::SUPERCEDED);

        try {
            const auto wifiInfo = quasar::net::getWifiInfo(device_);

            currentDiscoveryInfo_ = CurrentDiscoveryInfo();
            currentDiscoveryInfo_->password = wifiInfo.password;
            currentDiscoveryInfo_->ssid = quasar::getString(payload, "ssid");
            currentDiscoveryInfo_->deviceType = quasar::getString(payload, "device_type");
            if (payload["timeout_ms"].isInt()) {
                currentDiscoveryInfo_->timeout = std::chrono::milliseconds(quasar::getInt(payload, "timeout_ms"));
            }

            currentDiscovery_ = discoveryProvider_->createDiscovery();

            quasar::proto::IotDiscoveryResult result;
            result.set_code(quasar::proto::IotDiscoveryResult::UNKNOWN);

            transitionState(quasar::proto::IotState::STARTING_DISCOVERY, result);
            installCurrentDiscoveryTimeout(currentDiscoveryInfo_->timeout);
        } catch (const std::exception& e) {
            Json::Value value;
            value["reason"] = std::string(e.what());
            device_->telemetry()->reportEvent("iotDiscoveryFailedToStart", quasar::jsonToString(value));
            YIO_LOG_ERROR_EVENT("IotEndpoint.FailedStartDiscovery", "Failed to start IoT discovery: " << e.what());
            ensureDiscoveryStopped(quasar::proto::IotDiscoveryResult::NOT_STARTED);

            sdk_->getFilePlayerCapability()->playSoundFile("vins_error.wav", quasar::proto::AudioChannel::DIALOG_CHANNEL);
        }
    }

    void IotCapability::onDiscoveryCredentials(const Json::Value& payload) {
        if (iotState_ != quasar::proto::IotState::STARTING_DISCOVERY) {
            ensureDiscoveryStopped(quasar::proto::IotDiscoveryResult::CREDENTIALS_WITHOUT_DISCOVERY);
            return;
        }

        try {
            IIotDiscovery::PairingInfo pairingInfo;
            pairingInfo.wifiSSID = quasar::getString(payload, "ssid");
            pairingInfo.wifiPassword = quasar::getString(payload, "password");
            pairingInfo.token = quasar::getString(payload, "token");
            pairingInfo.cipher = quasar::getString(payload, "cipher");

            currentDiscovery_->start(pairingInfo);

            quasar::proto::IotDiscoveryResult result;
            result.set_code(quasar::proto::IotDiscoveryResult::UNKNOWN);
            transitionState(quasar::proto::IotState::DISCOVERY_IN_PROGRESS, result);
        } catch (const std::exception& e) {
            YIO_LOG_ERROR_EVENT("IotCapability.InvalidIotDiscoveryCredentialsPayload", "Can't get credentials: " << e.what());
            return;
        }
    }

    void IotCapability::onStopDiscovery(const Json::Value& payload) {
        const auto code = parseDiscoveryResultCode(quasar::tryGetString(payload, "result", "unknown"));
        ensureDiscoveryStopped(code);
    }

    void IotCapability::installCurrentDiscoveryTimeout(std::chrono::milliseconds timeout) {
        std::weak_ptr<YandexIO::IIotDiscovery> weakDiscovery = currentDiscovery_;
        callbackQueue_.addDelayed([this, weakDiscovery = std::move(weakDiscovery)] {
            auto discovery = weakDiscovery.lock();
            if (!discovery || discovery != currentDiscovery_) {
                return;
            }

            device_->telemetry()->reportEvent("iotDiscoveryTimeout");
            ensureDiscoveryStopped(quasar::proto::IotDiscoveryResult::TIMEOUT);
        }, timeout);
    }

    void IotCapability::ensureDiscoveryStopped(const quasar::proto::IotDiscoveryResult::ResultCode resultCode) {
        quasar::proto::IotDiscoveryResult result;
        result.set_code(resultCode);
        ensureDiscoveryStopped(result);
    }

    void IotCapability::ensureDiscoveryStopped(const quasar::proto::IotDiscoveryResult& reason) {
        if (currentDiscovery_) {
            currentDiscovery_->stop();
            currentDiscovery_.reset();
        }

        transitionState(quasar::proto::IotState::IDLE, reason);

        currentDiscoveryInfo_.reset();
    }

    void IotCapability::transitionState(quasar::proto::IotState::State newState, quasar::proto::IotDiscoveryResult result) {
        if (iotState_ == newState) {
            return;
        }

        YIO_LOG_INFO("Transition IOT state: from "
                     << quasar::proto::IotState::State_Name(iotState_)
                     << " to " << quasar::proto::IotState::State_Name(newState));

        // exit old state
        if (iotState_ == quasar::proto::IotState::STARTING_DISCOVERY) {
            device_->telemetry()->reportLatency(std::move(prepareLatencyPoint_), "iotDiscoveryPrepare");
        } else if (iotState_ == quasar::proto::IotState::DISCOVERY_IN_PROGRESS) {
            device_->telemetry()->reportLatency(std::move(waitLatencyPoint_), "iotDiscoveryStop");
        }

        // enter new state
        if (newState == quasar::proto::IotState::STARTING_DISCOVERY) {
            device_->telemetry()->reportEvent("iotDiscoveryStart");
            prepareLatencyPoint_ = device_->telemetry()->createLatencyPoint();
        } else if (newState == quasar::proto::IotState::DISCOVERY_IN_PROGRESS) {
            waitLatencyPoint_ = device_->telemetry()->createLatencyPoint();
        }

        if (newState == quasar::proto::IotState::STARTING_DISCOVERY) {
            auto event = createStartDiscoverySemanticFrame();
            auto request = std::make_shared<VinsRequest>(std::move(event), VinsRequest::createSoftwareDirectiveEventSource());
            sdk_->getAliceCapability()->startRequest(std::move(request), nullptr);

            if (auto sevents = events_.lock()) {
                sevents->onIotCapabilityDiscoveryStarted();
            }
        } else if (newState == quasar::proto::IotState::IDLE) {
            if (result.code() != quasar::proto::IotDiscoveryResult::SUCCESS) {
                auto event = createFailDiscoverySemanticFrame(result);
                auto request = std::make_shared<VinsRequest>(std::move(event), VinsRequest::createSoftwareDirectiveEventSource());
                sdk_->getAliceCapability()->startRequest(std::move(request), nullptr);
            }

            if (auto sevents = events_.lock()) {
                sevents->onIotCapabilityDiscoveryStopped();
            }
        } else if (newState == quasar::proto::IotState::DISCOVERY_IN_PROGRESS) {
            if (auto sevents = events_.lock()) {
                sevents->onIotCapabilityWifiCredentialsReceived();
            }
        }

        iotState_ = newState;
    }

    Json::Value IotCapability::createSemanticFrame(const NAlice::TSemanticFrameRequestData& data) {
        Json::Value semanticFrame;
        semanticFrame["name"] = "@@mm_semantic_frame";
        semanticFrame["type"] = "server_action";
        semanticFrame["payload"] = quasar::convertMessageToJson(data, true).value();

        return semanticFrame;
    }

    Json::Value IotCapability::createStartDiscoverySemanticFrame() const {
        NAlice::TSemanticFrameRequestData semanticFrame;

        auto analytics = semanticFrame.mutable_analytics();
        analytics->SetProductScenario("iot.voice_discovery");
        analytics->set_purpose("send_iot_pairing_token");
        analytics->set_origin(NAlice::TAnalyticsTrackingModule::SmartSpeaker);

        auto iotStartDiscoveryTsf = semanticFrame.mutable_typedsemanticframe()->mutable_iotdiscoverystartsemanticframe();
        iotStartDiscoveryTsf->mutable_ssid()->set_stringvalue(TString((*currentDiscoveryInfo_).ssid));
        iotStartDiscoveryTsf->mutable_password()->set_stringvalue(TString((*currentDiscoveryInfo_).password));
        iotStartDiscoveryTsf->mutable_devicetype()->set_stringvalue(TString((*currentDiscoveryInfo_).deviceType));
        iotStartDiscoveryTsf->mutable_timeoutms()->set_uint32value((*currentDiscoveryInfo_).timeout.count());

        return createSemanticFrame(semanticFrame);
    }

    Json::Value IotCapability::createFailDiscoverySemanticFrame(quasar::proto::IotDiscoveryResult result) const {
        NAlice::TSemanticFrameRequestData semanticFrame;

        auto analytics = semanticFrame.mutable_analytics();
        analytics->SetProductScenario("iot.voice_discovery");
        analytics->set_purpose("notify_about_broadcast_failure");
        analytics->set_origin(NAlice::TAnalyticsTrackingModule::SmartSpeaker);

        auto iotFailDiscoveryTsf = semanticFrame.mutable_typedsemanticframe()->mutable_iotdiscoveryfailuresemanticframe();
        iotFailDiscoveryTsf->mutable_timeoutms()->set_uint32value((*currentDiscoveryInfo_).timeout.count());
        iotFailDiscoveryTsf->mutable_reason()->set_stringvalue(TString(quasar::proto::IotDiscoveryResult::ResultCode_Name(result.code())));
        iotFailDiscoveryTsf->mutable_devicetype()->set_stringvalue(TString((*currentDiscoveryInfo_).deviceType));

        return createSemanticFrame(semanticFrame);
    }

    quasar::proto::IotDiscoveryResult::ResultCode IotCapability::parseDiscoveryResultCode(const std::string& code)
    {
        static const std::map<std::string, quasar::proto::IotDiscoveryResult::ResultCode> map = {
            {"success", quasar::proto::IotDiscoveryResult::SUCCESS},
            {"failure", quasar::proto::IotDiscoveryResult::FAILURE},
            {"timeout", quasar::proto::IotDiscoveryResult::TIMEOUT},
            {"superceded", quasar::proto::IotDiscoveryResult::SUPERCEDED},
            {"not_started", quasar::proto::IotDiscoveryResult::NOT_STARTED}};

        if (const auto it = map.find(code); it != map.end()) {
            return it->second;
        }

        return quasar::proto::IotDiscoveryResult::UNKNOWN;
    }

} // namespace YandexIO
