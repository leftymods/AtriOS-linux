#include "audio_client_endpoint.h"

#include <yandex_io/libs/cryptography/digest.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/protobuf_utils/proto_trace.h>

using namespace quasar;

namespace {
    constexpr std::chrono::seconds CLOCK_TOWER_SYNC_PERIOD{1};
} // namespace

AudioClientEndpoint::AudioClientEndpoint(
    std::shared_ptr<YandexIO::IDevice> device,
    std::shared_ptr<ipc::IIpcFactory> ipcFactory,
    std::shared_ptr<const IAudioClockManager> audioClockManager,
    std::shared_ptr<AudioPlayerFactory> audioPlayerFactory,
    std::shared_ptr<IMultiroomProvider> multiroomProvider,
    std::shared_ptr<IStereoPairProvider> stereoPairProvider)
    : device_(std::move(device))
    , server_(ipcFactory->createIpcServer(SERVICE_NAME))
    , syncdConnector_(ipcFactory->createIpcConnector("syncd"))
    , audioClockManager_(std::move(audioClockManager))
    , deviceContext_(ipcFactory, nullptr /* onConnected*/, false /* autoConnect*/)
    , clockTowerQueue_(std::make_shared<NamedCallbackQueue>("ClockTowerCallbackQueue"))
    , clockTowerUpdate_(clockTowerQueue_)
{
    Y_VERIFY(audioClockManager_);

    server_->setMessageHandler([this](const auto& message, auto& /*connection*/) {
        handleQuasarMessage(message);
    });

    server_->setClientConnectedHandler([this](auto& connection) {
        clockTowerUpdate(connection.share(), 0);
    });

    eventDistributor_ = std::make_shared<AudioEventDistributor>(server_, device_);

    const auto clientConfig = device_->configuration()->getServiceConfig("audioclient");
    Y_VERIFY(clientConfig.isMember("controller"));

    controller_ = std::make_unique<AudioClientController>(device_, ipcFactory, audioClockManager_,
                                                          std::move(multiroomProvider), std::move(stereoPairProvider), std::move(audioPlayerFactory), eventDistributor_, clientConfig["controller"]);

    syncdConnector_->setMessageHandler(std::bind(&AudioClientEndpoint::handleSyncMessage, this, std::placeholders::_1));
    syncdConnector_->connectToService();

    deviceContext_.onEqualizerConfig = [this](const quasar::proto::EqualizerConfig& config) {
        controller_->handleEqualizerRequest(config);
    };
    deviceContext_.connectToSDK();

    server_->listenService();
    clockTowerUpdate_.execute([this] { clockTowerUpdate(nullptr, 0); }, Lifetime::immortal);
}

AudioClientEndpoint::~AudioClientEndpoint() {
    server_->shutdown();
    clockTowerUpdate_.reset();
    clockTowerQueue_->destroy();
}

int AudioClientEndpoint::getPort() const {
    return server_->port();
}

void AudioClientEndpoint::handleQuasarMessage(const ipc::SharedMessage& message) {
    if (message->has_media_request()) {
        if (!message->media_request().has_stream_data()) {
            __PROTOTRACE("AudioClientEndpoint::handleQuasarMessage", *message);
        }
        controller_->handleMediaRequest(message->media_request());
    }
    if (message->has_add_clock_tower()) {
        const auto& clock = message->add_clock_tower();
        if (audioClockManager_->netAudioClock(clock.device_id(), clock.host(), clock.port(), clock.clock_id())) {
            clockTowerUpdate_.execute([this] { clockTowerUpdate(nullptr, 0); }, Lifetime::immortal);
        } else {
            YIO_LOG_WARN("Fail to create remote clock: " << convertMessageToDeepJsonString(*message));
        }
    }
}

void AudioClientEndpoint::handleSyncMessage(const ipc::SharedMessage& message) {
    if (!message->has_user_config_update()) {
        return;
    }
    const std::string& config = message->user_config_update().config();
    if (config.empty()) {
        return;
    }

    const Json::Value jsonConfig = parseJson(config);
    const Json::Value& systemConfig = jsonConfig["system_config"];

    if (systemConfig.isMember("audioclient")) {
        controller_->updateAudioClientConfig(systemConfig["audioclient"]);
    }
}

void AudioClientEndpoint::clockTowerUpdate(const std::shared_ptr<ipc::IServer::IClientConnection>& connection, uint64_t hash) {
    uint64_t newHash = 0;
    if (!connection) {
        XXH64Hasher hasher;
        auto localClock = audioClockManager_->localAudioClock();
        hasher.update(localClock->syncLevel());
        for (const auto& remoteClock : audioClockManager_->allNetAudioClocks()) {
            hasher.update(remoteClock->deviceId()).update(remoteClock->host()).update(remoteClock->port()).update(remoteClock->clockId()).update(remoteClock->syncLevel());
        }
        newHash = hasher.hash();
        if (hash && newHash == hash) {
            clockTowerUpdate_.executeDelayed([this, newHash] { clockTowerUpdate(nullptr, newHash); }, CLOCK_TOWER_SYNC_PERIOD, Lifetime::immortal);
            return;
        }
    }

    auto makeProtoClock =
        [&](const std::shared_ptr<const AudioClock>& audioClock)
    {
        constexpr int64_t NANOSECONDS_IN_SECOND = 1000000000;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        auto now = audioClock->now();
        auto diff = (ts.tv_sec * NANOSECONDS_IN_SECOND + ts.tv_nsec) - now.count();

        proto::ClockTowerSync::Clock protoClock;
        protoClock.set_device_id(TString(audioClock->deviceId()));
        protoClock.set_clock_host(TString(audioClock->host()));
        protoClock.set_clock_port(audioClock->port());
        protoClock.set_clock_id(TString(audioClock->clockId()));
        protoClock.set_diff_ns(diff);
        switch (audioClock->syncLevel()) {
            case AudioClock::SyncLevel::NONE:
                protoClock.set_sync_level(proto::ClockTowerSync::NOSYNC);
                break;
            case AudioClock::SyncLevel::WEAK:
                protoClock.set_sync_level(proto::ClockTowerSync::WEAK);
                break;
            case AudioClock::SyncLevel::STRONG:
                protoClock.set_sync_level(proto::ClockTowerSync::STRONG);
                break;
        }
        return protoClock;
    };

    auto message = ipc::buildMessage([&](auto& msg) {
        auto& clockSync = *msg.mutable_clock_tower_sync();
        auto audioClock = audioClockManager_->localAudioClock();
        clockSync.mutable_local_clock()->CopyFrom(makeProtoClock(audioClock));
        for (const auto& clock : audioClockManager_->allNetAudioClocks()) {
            clockSync.add_remote_clock()->CopyFrom(makeProtoClock(clock));
        }
    });

    if (connection) {
        connection->send(message);
    } else {
        YIO_LOG_DEBUG("ClockTowerUpdate: new hash " << newHash << ", old hash " << hash << ": " << convertMessageToDeepJsonString(*message));
        server_->sendToAll(message);
        clockTowerUpdate_.executeDelayed([this, newHash] { clockTowerUpdate(nullptr, newHash); }, CLOCK_TOWER_SYNC_PERIOD, Lifetime::immortal);
    }
}

AudioEventDistributor::AudioEventDistributor(std::shared_ptr<ipc::IServer> server, std::shared_ptr<YandexIO::IDevice> device)
    : device_(std::move(device))
    , server_(std::move(server))
{
}

void AudioEventDistributor::onAudioEvent(const proto::AudioClientEvent& event) {
    __PROTOTRACE("AudioEventDistributor::onAudioEvent", event);
    proto::QuasarMessage message;
    message.mutable_audio_client_event()->CopyFrom(event);
    server_->sendToAll(std::move(message));
}
