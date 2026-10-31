#include "media_endpoint.h"

#include "players_controller.h"
#include "suicide_exception.h"

#include <yandex_io/libs/audio_player/base/audio_player.h>
#include <yandex_io/libs/base/directives.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/equalizer_config/equalizer_config.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/protobuf_utils/debug.h>
#include <yandex_io/libs/protobuf_utils/proto_trace.h>
#include <yandex_io/libs/telemetry/telemetry.h>
#include <yandex_io/protos/enum_names/enum_names.h>
#include <yandex_io/protos/model_objects.pb.h>
#include <yandex_io/protos/quasar_proto.pb.h>

#include <future>
#include <memory>

YIO_DEFINE_LOG_MODULE("media");

using namespace quasar;

namespace {

    const int ON_AUTH_ERROR_MAX = 3;

    std::string makePlaybackErrorPayload(const std::string& vinsRequestId) {
        Json::Value payload = Json::objectValue;
        if (!vinsRequestId.empty()) {
            payload["vins_request_id"] = vinsRequestId;
        }
        return jsonToString(payload);
    }

} // namespace

MediaEndpoint::MediaEndpoint(
    std::shared_ptr<YandexIO::IDevice> device,
    std::shared_ptr<ipc::IIpcFactory> ipcFactory,
    std::shared_ptr<quasar::IAuthProvider> authProvider,
    std::shared_ptr<IDeviceStateProvider> deviceStateProvider,
    std::shared_ptr<const PlayerFactory> playerFactory,
    std::shared_ptr<YandexIO::IDeviceStateCapability> deviceState,
    std::shared_ptr<YandexIO::IFilePlayerCapability> filePlayerCapability)
    : device_(std::move(device))
    , authProvider_(std::move(authProvider))
    , server_(ipcFactory->createIpcServer(SERVICE_NAME))
    , deviceContext_(ipcFactory)
    , toSyncd_(ipcFactory->createIpcConnector("syncd"))
    , toBrickd_(ipcFactory->createIpcConnector("brickd"))
    , toAliced_(ipcFactory->createIpcConnector("aliced"))
    , deviceState_(std::move(deviceState))
    , filePlayerCapability_(std::move(filePlayerCapability))
{
    const auto config = device_->configuration()->getServiceConfig(SERVICE_NAME);
    abortOnFreezeTimeout_ = std::chrono::seconds(tryGetInt(config, "abortOnFreezeTimeout", 15));

    server_->setMessageHandler([this](const auto& message, auto& /*connection*/) {
        handleQuasarMessage(message);
    });

    playersController_ = std::make_shared<PlayersController>(device_, std::move(playerFactory));

    playersController_->setOnStateChangeHandler([=](const NAlice::TDeviceState& deviceState) {
        if (deviceState.HasMusic()) {
            deviceState_->setMusicState(deviceState.GetMusic());
        }
        if (deviceState.HasRadio()) {
            deviceState_->setRadioState(deviceState.GetRadio());
        }
    });

    playersController_->setOnPlayStartHandler([this](PlayerType playerType, const std::string& vinsRequestId) {
        if (playerType == PlayerType::YANDEX_MUSIC || playerType == PlayerType::YANDEX_RADIO) {
            auto message = ipc::buildMessage([&playerType, vinsRequestId](auto& msg) {
                msg.mutable_legacy_player_state_changed()->set_state(proto::LegacyPlayerStateChanged::STARTED);

                if (playerType == PlayerType::YANDEX_MUSIC) {
                    msg.mutable_legacy_player_state_changed()->set_player_type(proto::LegacyPlayerStateChanged::YANDEX_MUSIC);
                    try {
                        msg.mutable_legacy_player_state_changed()->set_vins_request_id(TString(vinsRequestId));
                    } catch (const std::exception& e) {
                        YIO_LOG_DEBUG("Failed to get music vins_request_id : " << e.what());
                    }
                } else {
                    msg.mutable_legacy_player_state_changed()->set_player_type(proto::LegacyPlayerStateChanged::YANDEX_RADIO);
                    try {
                        msg.mutable_legacy_player_state_changed()->set_vins_request_id(TString(vinsRequestId));
                    } catch (const std::exception& e) {
                        YIO_LOG_DEBUG("Failed to get radio vins_request_id : " << e.what());
                    }
                }
            });

            if (server_) {
                server_->sendToAll(message);
            }
        }
    });

    playersController_->setOnPlaybackError([this](const std::string& vinsRequestId) {
        YIO_LOG_ERROR_EVENT("MediaEndpoint.PlaybackError", "playback error from MPListener. isPlaying " << playersController_->isPlaying());
        if (playersController_->isPlaying()) {
            // invalidate player
            playersController_->resetCurrentPlayer();
            notifyOnPlaybackError(vinsRequestId);
            sendGoHome();

            device_->telemetry()->reportEvent("mediadPlaybackError", makePlaybackErrorPayload(vinsRequestId));
        }
    });

    playersController_->setOnAuthError(makeSafeCallback(
        [this](auto... /*args*/) {
            if (std::atomic_fetch_add(&onAuthErrorCounter_, 1) < ON_AUTH_ERROR_MAX) {
                authProvider_->requestAuthTokenUpdate("MediaEndpoint onAuthError " + std::to_string(onAuthErrorCounter_.load()));
            }
        }, lifetime_));

    playersController_->updatePlayerConfigs(config);

    toSyncd_->setMessageHandler([this](const auto& message) {
        if (message->has_user_config_update()) {
            const std::string& config = message->user_config_update().config();
            if (!config.empty()) {
                const Json::Value jsonConfig = parseJson(config);

                const Json::Value& systemConfig = jsonConfig["system_config"];

                const Json::Value& mediadConfig = systemConfig["mediad"];
                abortOnFreezeTimeout_ = std::chrono::seconds(tryGetInt(mediadConfig, "abortOnFreezeTimeout", 15));

                const Json::Value& notifyConfig = mediadConfig["notifyOnPlaybackError"];
                playbackSoundError_ = tryGetBool(notifyConfig, "sound", true);
                playbackLedError_ = tryGetBool(notifyConfig, "led", true);
                errorSoundTimeout_ = std::chrono::seconds(tryGetInt(notifyConfig, "soundTimeoutSec", 30));
                errorSoundDebounce_ = std::chrono::seconds(tryGetInt(notifyConfig, "soundDebounceSec", 10));

                if (playersController_) {
                    playersController_->updatePlayerConfigs(systemConfig);
                }
            }
        }
    });

    toBrickd_->setMessageHandler([this](const auto& message) {
        YIO_LOG_DEBUG("Got message from brickd");
        if (message->has_brick_status()) {
            auto brickStatus = message->brick_status();
            if (brickStatus == proto::BrickStatus::BRICK || brickStatus == proto::BrickStatus::BRICK_BY_TTL) {
                isBrick_ = true;
                playersController_->pause(false);
            } else {
                isBrick_ = false;
            }
        }
    });

    toAliced_->connectToService();
    toSyncd_->connectToService();
    toBrickd_->tryConnectToService();

    server_->listenService();

    deviceStateProvider->configurationChangedSignal().connect(
        [this](const auto& deviceState) {
            if (deviceState->configuration != DeviceState::Configuration::CONFIGURED) {
                playersController_->pause();
            }
        }, lifetime_);
    authProvider_->ownerAuthInfo().connect(
        [this](const auto& authInfo) {
            if (authInfo->passportUid != passportUid_) {
                onAuthErrorCounter_.store(0);
                if (!passportUid_.empty()) {
                    // if passportUid is empty we just received it first time
                    YIO_LOG_INFO("Restarting current player because of changing yandex uid");
                    playersController_->pause();
                    playersController_->discard();
                }
                passportUid_ = authInfo->passportUid;
            }
        }, lifetime_);

    deviceContext_.onEqualizerConfig = makeSafeCallback([this](const quasar::proto::EqualizerConfig& config) {
        playersController_->updateEqualizerConfig(
            YandexIO::EqualizerConfig::fromProto(config));
    }, lifetime_);
}

MediaEndpoint::~MediaEndpoint()
{
    lifetime_.die();
    playersController_.reset();
    server_->shutdown();
    auto shutdown = [](auto& connector) {
        connector->shutdown();
        connector->waitUntilDisconnected();
    };
    shutdown(toSyncd_);
    shutdown(toBrickd_);
}

void MediaEndpoint::handleQuasarMessage(const ipc::SharedMessage& message)
{
    if (lifetime_.expired()) {
        return;
    }
    if (!message->has_media_request()) {
        return;
    }

    std::future<void> result = std::async(std::launch::async, [this, &message]() {
        const auto& mediaRequest = message->media_request();
        const auto vinsInitRequestId = mediaRequest.vins_init_request_id();

        try {
            handleMediaRequest(mediaRequest);
        } catch (const SuicideException& e) {
            YIO_LOG_ERROR_EVENT("MediaEndpoint.Suicide", "SuicideException in handleQuasarMessage: " << e.what());
            device_->telemetry()->reportEvent("mediaEndpointSuicide", e.what());
            notifyOnPlaybackError(vinsInitRequestId);
            sleep(5); // sleep to give a chance for playing notification sound
            abort();
        } catch (const std::exception& e) {
            YIO_LOG_ERROR_EVENT("MediaEndpoint.FailedHandleMessage", "Exception in handleQuasarMessage: " << e.what());
            notifyOnPlaybackError(vinsInitRequestId);
        }
    });

    auto waitResult = result.wait_for(abortOnFreezeTimeout_.load());
    if (waitResult != std::future_status::ready) {
        std::string cause = shortUtf8DebugString(*message);
        YIO_LOG_INFO("ABORT!!! cause: " << cause);

        Json::Value eventValue;
        eventValue["cause"] = cause;
        device_->telemetry()->reportEvent("mediaEndpointAbort", jsonToString(eventValue));
        abort();
    } else {
        result.get();
    }
}

void MediaEndpoint::handleMediaRequest(const proto::MediaRequest& mediaRequest) {
    if (playersController_ == nullptr) {
        return;
    }

    lastMediaRequest_ = std::chrono::steady_clock::now();

    const std::string vinsRequestId = mediaRequest.vins_request_id();

    if (mediaRequest.has_play_audio()) {
        deviceContext_.fireMediaRequest(proto::MediaContentType::MUSIC);

        auto authInfo = authProvider_->ownerAuthInfo().value();
        while (!authInfo->isAuthorized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            YIO_LOG_DEBUG("Waiting for authorization");
            authInfo = authProvider_->ownerAuthInfo().value();
        }
        Json::Value options;
        options["uid"] = mediaRequest.uid();
        options["session_id"] = mediaRequest.session_id();
        options["device_id"] = device_->deviceId();
        if (mediaRequest.has_offset()) {
            options["offsetSec"] = mediaRequest.offset();
        }
        if (mediaRequest.has_vins_init_request_id()) {
            options["vins_init_request_id"] = mediaRequest.vins_init_request_id();
        }

        YIO_LOG_DEBUG("play_audio: " + jsonToString(options));
        options["token"] = authInfo->authToken;
        options["multiroom_token"] = mediaRequest.play_audio().multiroom_token();
        playersController_->play(PlayerType::YANDEX_MUSIC, options, vinsRequestId);
    }

    if (mediaRequest.has_play_radio()) {
        Json::Value options;
        options["id"] = mediaRequest.play_radio().id();
        options["uid"] = mediaRequest.play_radio().uid();
        options["url"] = mediaRequest.play_radio().url();
        options["title"] = mediaRequest.play_radio().title();
        options["color"] = mediaRequest.play_radio().color();
        options["image_url"] = mediaRequest.play_radio().image_url();
        if (mediaRequest.has_vins_init_request_id()) {
            options["vins_init_request_id"] = mediaRequest.vins_init_request_id();
        }

        playersController_->play(PlayerType::YANDEX_RADIO, options, vinsRequestId);
    }

    if (mediaRequest.has_pause()) {
        if (mediaRequest.has_fire_device_context() && mediaRequest.fire_device_context()) {
            playersController_->pause(true, vinsRequestId);
            deviceContext_.fireMediaPaused(proto::MediaContentType::MUSIC);
        } else {
            playersController_->pause(mediaRequest.pause().smooth(), vinsRequestId);
        }
    }

    if (mediaRequest.has_resume()) {
        playersController_->resume(true, vinsRequestId);
    }

    if (mediaRequest.has_replay()) {
        playersController_->seekTo(0, vinsRequestId);
        if (!playersController_->isPlaying()) {
            playersController_->resume(true, vinsRequestId);
        }
    }

    if (mediaRequest.has_take_audio_focus()) {
        playersController_->takeAudioFocus();
    }

    if (mediaRequest.has_free_audio_focus()) {
        playersController_->freeAudioFocus();
    }

    const bool fireDeviceContext = mediaRequest.has_fire_device_context() && mediaRequest.fire_device_context();

    if (mediaRequest.has_rewind()) {
        const auto& rewind = mediaRequest.rewind();
        if (rewind.has_type() && rewind.has_amount()) {
            if (rewind.type() == proto::MediaRequest::ABSOLUTE) {
                playersController_->seekTo(rewind.amount(), vinsRequestId);
            } else if (rewind.type() == proto::MediaRequest::FORWARD) {
                playersController_->seekForward(rewind.amount(), vinsRequestId);
            } else if (rewind.type() == proto::MediaRequest::BACKWARD) {
                playersController_->seekBackward(rewind.amount(), vinsRequestId);
            }
        }
    }

    if (mediaRequest.has_like()) {
        if (fireDeviceContext) {
            deviceContext_.fireMediaLiked(proto::MediaContentType::MUSIC);
        }
        playersController_->processCommand(commands::LIKE, Json::Value());
    }

    if (mediaRequest.has_dislike()) {
        if (fireDeviceContext) {
            deviceContext_.fireMediaDisliked(proto::MediaContentType::MUSIC);
        }
        playersController_->processCommand(commands::DISLIKE, Json::Value());

        Json::Value nextOptions;
        nextOptions["skip"] = true;
        playersController_->processCommand(commands::NEXT, nextOptions);
    }

    if (mediaRequest.has_next()) {
        const auto& command = mediaRequest.next();
        if (fireDeviceContext) {
            if (!playersController_->isPlaying()) {
                deviceContext_.fireMediaResumed(proto::MediaContentType::MUSIC);
            }
            deviceContext_.fireMediaSwitchedForward(proto::MediaContentType::MUSIC);
        }

        Json::Value options;
        options["skip"] = true;
        options["setPause"] = command.has_set_pause() && command.set_pause();
        playersController_->processCommand(commands::NEXT, options);
    }

    if (mediaRequest.has_previous()) {
        const auto& command = mediaRequest.previous();
        if (fireDeviceContext) {
            if (!playersController_->isPlaying()) {
                deviceContext_.fireMediaResumed(proto::MediaContentType::MUSIC);
            }
            deviceContext_.fireMediaSwitchedBackward(proto::MediaContentType::MUSIC);
        }

        Json::Value options;
        options["forced"] = command.has_forced() && command.forced();
        options["setPause"] = command.has_set_pause() && command.set_pause();
        playersController_->processCommand(commands::PREVIOUS, options);
    }

    if (mediaRequest.has_equalizer_config()) {
        playersController_->updateEqualizerConfig(
            YandexIO::EqualizerConfig::fromProto(mediaRequest.equalizer_config()));
    }
}

void MediaEndpoint::notifyOnPlaybackError(const std::string& vinsRequestId) {
    if (playbackLedError_) {
        /* XXX: Hack: we know that media error is used to draw Leds now.
         *      Need to handle config from maind, or make this animation default (without runtime flag)
         */
        deviceContext_.fireMediaError(proto::MediaContentType::MUSIC);
    }
    if (playbackSoundError_) {
        auto now = std::chrono::steady_clock::now();
        bool hasMediaRequest = now - lastMediaRequest_ < errorSoundTimeout_.load();
        bool isPlayedErrorSoundRecently = now - lastErrorSound_ < errorSoundDebounce_.load();
        if (hasMediaRequest && !isPlayedErrorSoundRecently) {
            lastErrorSound_ = now;
            YandexIO::IFilePlayerCapability::PlayParams playParams{
                .parentRequestId = vinsRequestId};
            filePlayerCapability_->playSoundFile("playback_error.mp3", proto::AudioChannel::DIALOG_CHANNEL, std::move(playParams));
            device_->telemetry()->reportEvent("notifyOnPlaybackError", makePlaybackErrorPayload(vinsRequestId));
        }
    }
}

void MediaEndpoint::sendGoHome() {
    auto message = ipc::buildMessage([&](auto& msg) {
        msg.mutable_directive()->set_name(Directives::GO_HOME);
    });
    toAliced_->sendMessage(message);
}

const std::string MediaEndpoint::SERVICE_NAME = "mediad";
