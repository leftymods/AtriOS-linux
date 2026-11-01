#include "command_server.h"

#include "command_result.h"
#include "common.h"
#include "log_streamer.h"

#include <yandex_io/libs/base/directive_types.h>
#include <yandex_io/libs/base/directives.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/defines.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/logging/setup/setup.h>
#include <yandex_io/libs/speechkit_logger/speechkit_logger.h>

#include <speechkit/CompositeSoundBuffer.h>
#include <speechkit/Logger.h>
#include <speechkit/SoundInfo.h>
#include <speechkit/SpeechKit.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

YIO_DEFINE_LOG_MODULE("audio_sender");

using namespace quasar;
using namespace AudioSender;
using namespace YandexIO;

namespace {

    Json::Value prepareResponseJson(const AudioSender::CommandResult& result,
                                    const std::string& recordId,
                                    const std::string& command) {
        Json::Value responseJson;
        responseJson["result"] = result.result;
        if (!result) {
            responseJson["errorMessage"] = result.message;
        }
        if (!recordId.empty()) {
            responseJson["recordId"] = recordId;
        }
        responseJson["command"] = command;
        return responseJson;
    }

    bool validateConfig(const Json::Value& /* config */) {
        try {
            // TODO
        } catch (const std::exception& e) {
            return false;
        }

        return true;
    }

    uint64_t calculateTimeMs(const std::list<SpeechKit::SoundBuffer::SharedPtr>& buffers) {
        uint64_t result = 0;
        for (const auto& buffer : buffers) {
            result += buffer->calculateTimeMs();
        }
        return result;
    }

    constexpr char COMMAND_NAME_PREPARE[] = "prepare";
    constexpr char COMMAND_NAME_RECORD[] = "record";
    constexpr char COMMAND_NAME_STOP_RECORDING[] = "stopRecording";
    constexpr char COMMAND_NAME_PLAY[] = "play";
    constexpr char COMMAND_NAME_STOP_PLAYING[] = "stopPlaying";
    constexpr char COMMAND_NAME_SEND[] = "send";
    constexpr char COMMAND_NAME_PLAY_MUSIC[] = "playMusic";
    constexpr char COMMAND_NAME_PLAY_FILE[] = "playFile";
    constexpr char COMMAND_NAME_STOP_MUSIC[] = "stopMusic";
    constexpr char COMMAND_NAME_GET_VOLUME[] = "getVolume";
    constexpr char COMMAND_NAME_SET_VOLUME[] = "setVolume";
    constexpr char COMMAND_NAME_GET_RECORDED_SOUND[] = "getRecordedSound";
    constexpr char COMMAND_NAME_GET_AUDIO_CONFIG[] = "getAudioConfig";
    constexpr char COMMAND_NAME_SUBSCRIBE_LOGS[] = "subscribeLogs";
    constexpr char COMMAND_NAME_UNSUBSCRIBE_LOGS[] = "unsubscribeLogs";
    constexpr char COMMAND_NAME_GET_CONFIG[] = "getConfig";
    constexpr char COMMAND_NAME_SET_CONFIG[] = "setConfig";
    constexpr char COMMAND_NAME_START_STREAMING[] = "startStreaming";
    constexpr char COMMAND_NAME_STOP_STREAMING[] = "stopStreaming";
    constexpr char COMMAND_NAME_RESTART[] = "restart";

    const uint16_t COMMAND_PORT = 8787;

} // namespace

MetricaEventLogger::MetricaEventLogger(std::shared_ptr<YandexIO::ITelemetry> telemetry)
    : telemetry_(std::move(telemetry))
{
}

void MetricaEventLogger::reportEvent(const std::string& eventName, const EventArgs& args) {
    std::unordered_map<std::string, std::string> keyValues(args.begin(), args.end());
    telemetry_->reportKeyValues(eventName, keyValues);
}

CommandServer::CommandServer(
    const Json::Value& config,
    std::shared_ptr<YandexIO::IDevice> inDevice,
    std::shared_ptr<quasar::ipc::IIpcFactory> inIpcFactory,
    std::weak_ptr<YandexIO::IEndpointStorage> endpointStorage,
    AudioSourceAdapter::SharedPtr adapter,
    SpeechKit::AudioPlayer::SharedPtr audioPlayer,
    const std::string& runtimeConfigPath,
    std::shared_ptr<quasar::IAuthProvider> authProvider)
    : config(config)
    , runtimeConfigPath(runtimeConfigPath)
    , authProvider_(std::move(authProvider))
    , device(std::move(inDevice))
    , endpointStorage_(std::move(endpointStorage))
    , ipcFactory(std::move(inIpcFactory))
    , audioConfig(std::make_shared<AudioConfig>(config))
    , quasarAudioSourceAdapter(std::move(adapter))
    , sendLogTimeout(std::chrono::seconds(quasar::tryGetInt(config, "sendLogTimeoutSec", 10)))
    , state(State::IDLE)
    , uniProxyUrl(quasar::tryGetString(config, "uniProxyUrl", "wss://beta.uniproxy.alice.yandex.net/alice-uniproxy-hamster/uni.ws"))
{
    soundPlayer = std::make_shared<SoundPlayer>(std::move(audioPlayer));
    soundCollector = std::make_shared<SoundCollector>(audioConfig, quasarAudioSourceAdapter, device);
    streamSoundCollector = std::make_shared<StreamSoundCollector>(audioConfig, quasarAudioSourceAdapter, endpoint);
    phraseSpotter = std::make_shared<PhraseSpotterWrapper>(quasar::tryGetString(config, "spotterModelPath", ""));
    eventLogger = std::make_shared<MetricaEventLogger>(device->telemetry());
    rebootCommand = quasar::tryGetString(device->configuration()->getCommonConfig(), "reboot_script", "reboot");
}

CommandServer::~CommandServer() {
    stop();
}

void CommandServer::start() {
    SpeechKit::SoundLogger::getInstance()->setSettings(
        uniProxyUrl, std::chrono::milliseconds::zero());
    SpeechKit::SpeechKit::getInstance()->setEventLogger(eventLogger);

    endpoint->init_asio();

    auto logLevel = quasar::tryGetString(config, "logLevel", "error");
    if (logLevel != "debug" && logLevel != "trace") {
        endpoint->clear_access_channels(websocketpp::log::alevel::all);
    }

    endpoint->set_message_handler(std::bind(
        &CommandServer::command_handler,
        this,
        std::placeholders::_1,
        std::placeholders::_2));
    endpoint->set_fail_handler([this](websocketpp::connection_hdl connectionHandler) {
        YIO_LOG_ERROR_EVENT("CommandServer.ConnectionFailed", endpoint->get_con_from_hdl(connectionHandler)->get_ec().message());
    });
    endpoint->set_close_handler([](websocketpp::connection_hdl /* connectionHandler */) {
        YIO_LOG_ERROR_EVENT("CommandServer.ConnectionClosed", "");
    });
    endpoint->set_reuse_addr(true);
    endpoint->listen(COMMAND_PORT);
    endpoint->start_accept();
    asioServiceThread = std::thread([this]() {
        endpoint->run();
    });
    YIO_LOG_INFO("AudioSender command server started.");
}

void CommandServer::stop() {
    // stop listening new connections
    endpoint->stop_listening();
    // clear perpetual flag (automatically exit when all connections closed)
    endpoint->stop_perpetual();
    // stop internal io service
    endpoint->stop();
    // wait io service stopped
    if (asioServiceThread.joinable()) {
        asioServiceThread.join();
    }
}

void CommandServer::onCapabilityStateChanged(const std::shared_ptr<YandexIO::ICapability>& /*capability*/, const NAlice::TCapabilityHolder& state) {
    if (!state.HasDeviceStateCapability() || !state.GetDeviceStateCapability().GetState().HasDeviceState()) {
        return;
    }

    if (!state.GetDeviceStateCapability().GetState().GetDeviceState().HasSoundLevel()) {
        return;
    }

    const auto volume = state.GetDeviceStateCapability().GetState().GetDeviceState().GetSoundLevel();
    YIO_LOG_DEBUG("Received device state with volume level (via DeviceStateCapability): " << volume);

    std::scoped_lock lock(volumeMutex_);
    volume_ = volume;
    volumeAcknowledgeCV_.notify_one();
}

void CommandServer::onCapabilityEvents(const std::shared_ptr<YandexIO::ICapability>& /*capability*/, const std::vector<NAlice::TCapabilityEvent>& /*events*/) {
    // ¯\_(ツ)_/¯
}

void CommandServer::onMusicState(const quasar::proto::AppState::MusicState& musicState) {
    if (musicPlayer == nullptr) {
        return;
    }

    musicPlayer->onMusicState(musicState);
}

void CommandServer::command_handler(websocketpp::connection_hdl handler,
                                    websocketpp::server<websocketpp::config::asio>::message_ptr message) {
    if (message->get_opcode() == websocketpp::frame::opcode::binary) {
        processBinaryMessage(handler, message);
        return;
    }

    YIO_LOG_DEBUG("New text message. Payload: " << message->get_payload());
    Json::Value commandJson;
    std::istringstream jsonIn(message->get_payload());
    try {
        jsonIn >> commandJson;
    } catch (const Json::RuntimeError& e) {
        YIO_LOG_ERROR_EVENT("CommandServer.InvalidRequest", "JSON parsing error: " << e.what());
        CommandResult result{false, "JSON parsing error"};
        endpointSend(handler, prepareResponseJson(result, "", ""));
        return;
    }

    const auto command = commandJson.get("command", "error").asString();
    std::string refMessageId = commandJson.get("messageId", "unspecified").asString();
    Json::Value response;
    if (command == COMMAND_NAME_PREPARE) {
        response = processPrepare();
    } else if (command == COMMAND_NAME_RECORD) {
        response = processRecord(commandJson, handler);
    } else if (command == COMMAND_NAME_STOP_RECORDING) {
        response = processStopRecording(commandJson);
    } else if (command == COMMAND_NAME_PLAY) {
        response = processPlay(commandJson, handler);
    } else if (command == COMMAND_NAME_STOP_PLAYING) {
        response = processStopPlaying(commandJson);
    } else if (command == COMMAND_NAME_SEND) {
        response = processSend(commandJson, handler);
    } else if (command == COMMAND_NAME_PLAY_MUSIC) {
        response = processPlayMusic(commandJson);
    } else if (command == COMMAND_NAME_PLAY_FILE) {
        response = processPlayFile(commandJson);
    } else if (command == COMMAND_NAME_STOP_MUSIC) {
        response = processStopMusic();
    } else if (command == COMMAND_NAME_GET_VOLUME) {
        response = processGetVolume();
    } else if (command == COMMAND_NAME_SET_VOLUME) {
        response = processSetVolume(commandJson);
    } else if (command == COMMAND_NAME_GET_RECORDED_SOUND) {
        response = processGetRecordedSound(commandJson, handler);
    } else if (command == COMMAND_NAME_GET_AUDIO_CONFIG) {
        response = processGetAudioConfig();
    } else if (command == COMMAND_NAME_SUBSCRIBE_LOGS) {
        response = processSubscribeLogs(commandJson, handler);
    } else if (command == COMMAND_NAME_UNSUBSCRIBE_LOGS) {
        response = processUnsubscribeLogs();
    } else if (command == COMMAND_NAME_GET_CONFIG) {
        response = processGetConfig();
    } else if (command == COMMAND_NAME_SET_CONFIG) {
        response = processSetConfig(commandJson);
    } else if (command == COMMAND_NAME_START_STREAMING) {
        response = processStartStreaming(commandJson, handler);
    } else if (command == COMMAND_NAME_STOP_STREAMING) {
        response = processStopStreaming();
    } else if (command == COMMAND_NAME_RESTART) {
        processRestart(handler);
    } else {
        YIO_LOG_WARN("Warning: unknown command");
    }

    if (!response.empty()) {
        response["refMessageId"] = refMessageId;
        endpointSend(handler, response);
    }
}

Json::Value CommandServer::processPrepare() {
    YIO_LOG_DEBUG("PREPARE");

    SpeechKit::SoundLogger::getInstance()->reconnect();
    return prepareResponseJson({true}, "", COMMAND_NAME_PREPARE);
}

void CommandServer::processBinaryMessage(websocketpp::connection_hdl handler,
                                         websocketpp::server<websocketpp::config::asio>::message_ptr message) {
    if (!soundUploadRequest.isActive) {
        YIO_LOG_WARN("Received unexpected binary message. Ignoring.");
        return;
    }

    std::copy(message->get_payload().begin(),
              message->get_payload().end(),
              std::back_inserter(soundUploadRequest.rawData));

    if (soundUploadRequest.rawData.size() < soundUploadRequest.expectedDataSize) {
        return;
    }

    // Send response and play received sound
    auto soundBufferPtr = std::make_shared<SpeechKit::SoundBuffer>(soundUploadRequest.soundInfo,
                                                                   std::move(soundUploadRequest.rawData));

    soundPlayer->start({soundBufferPtr}, [](bool /*unused*/) {});

    soundUploadRequest.isActive = false;
    auto response = prepareResponseJson({true}, "", COMMAND_NAME_PLAY_FILE);
    response["refMessageId"] = soundUploadRequest.refMessageId;
    endpointSend(handler, response);
}

Json::Value CommandServer::processPlayFile(const Json::Value& request) {
    if (!request.isMember("soundInfo")) {
        return prepareResponseJson({false, "SoundInfo was not provided"}, "", COMMAND_NAME_PLAY_FILE);
    }
    soundPlayer->stop();

    const Json::Value& soundInfoJson = request["soundInfo"];
    soundUploadRequest.soundInfo = SpeechKit::SoundInfo(SpeechKit::SoundFormat::PCM,
                                                        soundInfoJson.get("channelCount", 1).asInt(),
                                                        soundInfoJson.get("sampleRate", 16000).asInt(),
                                                        soundInfoJson.get("sampleSize", 2).asInt());

    soundUploadRequest.expectedDataSize = soundInfoJson.get("size", 0).asUInt64();
    if (soundUploadRequest.expectedDataSize == 0) {
        return prepareResponseJson({false, "data size was not provided"}, "", COMMAND_NAME_PLAY_FILE);
    }

    soundUploadRequest.rawData.clear();
    soundUploadRequest.rawData.reserve(soundUploadRequest.expectedDataSize);
    soundUploadRequest.refMessageId = request.get("messageId", "unspecified").asString();
    soundUploadRequest.isActive = true;

    return {};
}

Json::Value CommandServer::processRecord(const Json::Value& request, websocketpp::connection_hdl handler) {
    const auto timestamp = request.get("timestamp", 0).asUInt64();
    const auto recordId = request.get("recordId", "00000000-0000-0000-0000-000000000000").asString();
    YIO_LOG_DEBUG("RECORD PHRASE "
                  << " WITH ID " << recordId << " TIMESTAMP: " << timestamp);

    SpeechKit::SpeechKit::getInstance()->setApiKey(request.get("apiKey", "").asString());
    SpeechKit::SpeechKit::getInstance()->setUuid(request.get("uuid", "").asString());

    soundLogPayload = request["payload"];

    std::unordered_set<std::string> dumpingChannels;
    if (request.isMember("dumpingChannelNames")) {
        const auto& dumpingChannelsJson = request["dumpingChannelNames"];
        for (auto it = dumpingChannelsJson.begin(); it != dumpingChannelsJson.end(); ++it) {
            dumpingChannels.insert(it->asString());
        }
    } else {
        dumpingChannels = audioConfig->getDumpingChannelNames();
    }
    CommandResult result = setDumpingChannels(dumpingChannels);
    if (!result) {
        return prepareResponseJson(result, recordId, COMMAND_NAME_RECORD);
    }

    if (state == State::STREAMING) {
        streamSoundCollector->stopRecording();
    }
    soundCollector->enable();
    std::string metricType = soundLogPayload["extra"]["metricType"].asString();
    if (metricType == "spotterFRR") {
        YIO_LOG_DEBUG("Received [spotterFRR]. Starting spotter...");
        state = State::SPOTTER_MANUAL_SEND;
        result = phraseSpotter->startManualSend(quasarAudioSourceAdapter);
        if (result) {
            result = soundCollector->startRecording(timestamp);
        }
    } else if (metricType == "spotterFAh") {
        YIO_LOG_DEBUG("Received [spotterFAh]. Starting spotter...");
        state = State::SPOTTER_AUTO_SEND;
        logSendingResultSink = std::make_shared<LogSendingResultSink>(
            [this, handler, recordId](bool success, const std::string& error,
                                      const std::vector<std::string>& messageIds)
            {
                if (!success) {
                    YIO_LOG_ERROR_EVENT("SoundLogger.FAhSendingFailed", error);
                }
                auto json = prepareResponseJson({success, error}, recordId, COMMAND_NAME_SEND);
                if (!messageIds.empty()) {
                    json["messageId"] = messageIds.back(); // TODO: remove after vacert & VC switch to messageIds
                    for (const auto& messageId : messageIds) {
                        json["messageIds"].append(messageId);
                    }
                }
                endpointSend(handler, json);
            });
        SpeechKit::SoundLogger::getInstance()->setGlobalSink(logSendingResultSink);
        auto soundBeforeTrigger = std::chrono::milliseconds(request.isMember("soundLengthBeforeTriggerMs") ? request["soundLengthBeforeTriggerMs"].asInt() : 5000);
        auto soundAfterTrigger = std::chrono::milliseconds(request.isMember("soundLengthAfterTriggerMs") ? request["soundLengthAfterTriggerMs"].asInt() : 1000);
        result = phraseSpotter->startAutoSend(quasarAudioSourceAdapter,
                                              Json::FastWriter().write(soundLogPayload["extra"]),
                                              soundBeforeTrigger,
                                              soundAfterTrigger);
    } else {
        YIO_LOG_DEBUG("StartManualSend");
        state = State::MANUAL_SEND;
        result = soundCollector->startRecording(timestamp);
    }

    return prepareResponseJson(result, recordId, COMMAND_NAME_RECORD);
}

Json::Value CommandServer::processStopRecording(const Json::Value& request) {
    const auto timestamp = request.get("timestamp", 0).asUInt64();
    const auto recordId = request.get("recordId", "00000000-0000-0000-0000-000000000000").asString();
    YIO_LOG_DEBUG("STOP " << recordId);
    phraseSpotter->stop();
    CommandResult result;
    switch (state) {
        case State::IDLE:
        case State::SPOTTER_AUTO_SEND:
            break;
        case State::SPOTTER_MANUAL_SEND:
        case State::MANUAL_SEND:
        default:
            result = soundCollector->stopRecording(timestamp);
    }
    return prepareResponseJson(result, recordId, COMMAND_NAME_STOP_RECORDING);
}

Json::Value CommandServer::processPlay(const Json::Value& request, websocketpp::connection_hdl handler) {
    const auto recordId = request.get("recordId", "00000000-0000-0000-0000-000000000000").asString();
    YIO_LOG_DEBUG("PLAY " << recordId);
    SoundCollector::ChannelSound channelSounds;
    auto result = soundCollector->getRecordedSound(channelSounds);
    if (result) {
        const auto it = channelSounds.find(audioConfig->getPlayerChannelName());
        if (it == channelSounds.end()) {
            result = {false, "There is no channels for playing"};
        } else {
            soundPlayer->start(it->second, [this, handler, recordId](bool result) {
                auto json = prepareResponseJson(result ? CommandResult{true, ""} : CommandResult{false, "Playing error"},
                                                recordId, COMMAND_NAME_PLAY);
                if (result) {
                    json["extra"] = "playingCompleted";
                }
                endpointSend(handler, json);
            });
        }
    }
    return prepareResponseJson(result, recordId, COMMAND_NAME_PLAY);
}

Json::Value CommandServer::processStopPlaying(const Json::Value& request) {
    const auto recordId = request.get("recordId", "00000000-0000-0000-0000-000000000000").asString();
    YIO_LOG_DEBUG("STOP PLAYING " << recordId);
    soundPlayer->stop();
    return prepareResponseJson(CommandResult(), recordId, COMMAND_NAME_STOP_PLAYING);
}

Json::Value CommandServer::processSend(const Json::Value& request, websocketpp::connection_hdl handler) {
    const auto recordId = request.get("recordId", "00000000-0000-0000-0000-000000000000").asString();
    YIO_LOG_DEBUG("SEND " << recordId);

    switch (state) {
        case State::SPOTTER_AUTO_SEND:
            return processAutoSend(recordId);
        case State::MANUAL_SEND:
        case State::SPOTTER_MANUAL_SEND:
        default:
            return processManualSend(recordId, handler);
    }
}

Json::Value CommandServer::processAutoSend(const std::string& recordId) {
    logSendingResultSink->setRequiredCount(phraseSpotter->getTriggeringCount());
    if (logSendingResultSink->waitFor(sendLogTimeout) == std::future_status::timeout) {
        return prepareResponseJson({false, "Sending log timeout"}, recordId, COMMAND_NAME_SEND);
    }

    if (logSendingResultSink->getResult()) {
        auto json = prepareResponseJson(CommandResult(), recordId, COMMAND_NAME_SEND);
        json["extra"]["triggeringCount"] = phraseSpotter->getTriggeringCount();
        return json;
    }

    return Json::Value();
}

Json::Value CommandServer::processManualSend(const std::string& recordId, websocketpp::connection_hdl handler) {
    SoundCollector::ChannelSound channelSounds;
    auto result = soundCollector->getRecordedSound(channelSounds);
    if (!result) {
        return prepareResponseJson(result, recordId, COMMAND_NAME_SEND);
    }

    if (state == State::SPOTTER_MANUAL_SEND) {
        soundLogPayload["extra"]["triggeringCount"] = phraseSpotter->getTriggeringCount();
    }
    soundLogPayload["firmware"] = SpeechKit::SpeechKit::getInstance()->getPlatformInfo()->getFirmwareVersion();

    auto soundLogger = SpeechKit::SoundLogger::getInstance();

    Json::Value stats;
    stats["absolute"] = soundCollector->getRecordingTimeMs();
    std::vector<std::string> messageIds;
    messageIds.reserve(channelSounds.size());
    for (const auto& channelToSound : channelSounds) {
        const auto channelDuration = calculateTimeMs(channelToSound.second);
        stats[channelToSound.first] = channelDuration;
        auto channelPayload = soundLogPayload;
        channelPayload["extra"]["channelName"] = channelToSound.first;
        channelPayload["extra"]["stats"]["absolute"] = soundCollector->getRecordingTimeMs();
        channelPayload["extra"]["stats"]["current"] = channelDuration;

        std::list<SpeechKit::CompositeSoundBuffer::SharedPtr> compositeBuffers;
        for (auto& soundBuffer : channelToSound.second) {
            auto compositeBuffer = std::make_shared<SpeechKit::CompositeSoundBuffer>(channelToSound.first, soundBuffer);
            compositeBuffers.push_back(compositeBuffer);
        }
        const SpeechKit::SoundLogger::Entry soundLogEntry{compositeBuffers, Json::FastWriter().write(channelPayload)};

        sendingExpectation = std::promise<void>();
        auto sink = std::make_shared<LogSendingResultSink>(
            [this, handler, recordId, &messageIds](bool success, const std::string& error,
                                                   const std::vector<std::string>& messageIdsArg)
            {
                if (success) {
                    messageIds.insert(messageIds.end(), messageIdsArg.begin(), messageIdsArg.end());
                    sendingExpectation.set_value();
                } else {
                    YIO_LOG_ERROR_EVENT("SoundLogger.ManualSendingFailed", error);
                    sendingExpectation.set_exception(std::make_exception_ptr(std::runtime_error(error)));
                }
            });
        soundLogger->sendLog(soundLogEntry, sink);

        auto future = sendingExpectation.get_future();
        auto waitingResult = future.wait_for(sendLogTimeout);
        if (waitingResult != std::future_status::ready) {
            return prepareResponseJson({false, "Sending timeout"}, recordId, COMMAND_NAME_SEND);
        }
        try {
            future.get();
        } catch (std::runtime_error& err) {
            return prepareResponseJson({false, err.what()}, recordId, COMMAND_NAME_SEND);
        }
    }

    auto json = prepareResponseJson({}, recordId, COMMAND_NAME_SEND);
    for (const auto& messageId : messageIds) {
        json["messageIds"].append(messageId);
    }
    if (state == State::SPOTTER_MANUAL_SEND) {
        json["extra"]["triggeringCount"] = phraseSpotter->getTriggeringCount();
    }
    json["extra"]["stats"] = stats;
    return json;
}

Json::Value CommandServer::processPlayMusic(const Json::Value& request) {
    YIO_LOG_DEBUG("PLAY MUSIC");

    std::call_once(musicPlayerFlag, [this, &request]() {
        auto uuid = request.get("uuid", "54a38daa-bd2e-4163-82ff-3e1d715f98f9").asString();
        auto apiKey = request.get("apiKey", "3e6b7426-c907-42ac-927c-dcb4954ec98f").asString();
        musicPlayer = YandexMusicPlayer::create(ipcFactory, uuid, apiKey, uniProxyUrl, authProvider_);
    });

    const std::string music = request.get("music", "").asString();
    const int offset = request.get("offset", 0).asInt();

    const auto result = musicPlayer->play(music, offset);
    auto responseJson = prepareResponseJson(result, "", COMMAND_NAME_PLAY_MUSIC);

    std::scoped_lock lock(volumeMutex_);
    responseJson["extra"] = static_cast<double>(volume_);
    return responseJson;
}

Json::Value CommandServer::processStopMusic() {
    YIO_LOG_DEBUG("STOP MUSIC");

    soundPlayer->stop();
    if (musicPlayer) {
        musicPlayer->stop();
    }

    return prepareResponseJson({true}, "", COMMAND_NAME_STOP_MUSIC);
}

Json::Value CommandServer::processGetVolume() {
    YIO_LOG_DEBUG("GET VOLUME");
    auto responseJson = prepareResponseJson(CommandResult{}, "", COMMAND_NAME_GET_VOLUME);

    std::scoped_lock lock(volumeMutex_);
    responseJson["extra"] = static_cast<double>(volume_);
    return responseJson;
}

Json::Value CommandServer::processSetVolume(const Json::Value& request) {
    YIO_LOG_DEBUG("SET VOLUME");
    if (!request.isMember("value")) {
        return prepareResponseJson({false, "No volume value in request"}, "", COMMAND_NAME_SET_VOLUME);
    }

    const int requestVolume = std::round(request["value"].asDouble());
    auto result = setVolume(requestVolume);
    return prepareResponseJson(result, "", COMMAND_NAME_SET_VOLUME);
}

CommandResult CommandServer::setVolume(int aliceVolume) {
    const auto sstorage = endpointStorage_.lock();
    if (sstorage == nullptr) {
        return CommandResult{false, "Volume wasn't set up by Quasar/Vendor volume handler due to absent EndpointStorage"};
    }

    const auto volumeCapability = sstorage->getLocalEndpoint()->findCapabilityById("VolumeCapability");
    if (volumeCapability == nullptr) {
        return CommandResult{false, "Volume wasn't set up by Quasar/Vendor volume handler due to absent VolumeCapability"};
    }

    Json::Value directivePayload;
    directivePayload["new_level"] = aliceVolume;
    const auto setVolumeDirective = std::make_shared<Directive>(
        YandexIO::Directive::Data{Directives::SOUND_SET_LEVEL, DirectiveTypes::CLIENT_ACTION, std::move(directivePayload)});
    volumeCapability->getDirectiveHandler()->handleDirective(setVolumeDirective);

    /* Make sure that volume is set up by Quasar/Vendor */
    std::unique_lock lock(volumeMutex_);
    const bool res = volumeAcknowledgeCV_.wait_for(lock, std::chrono::seconds(3), [this, aliceVolume]() {
        return volume_ == aliceVolume;
    });

    if (res) {
        return {};
    }
    return CommandResult{false, "Volume wasn't set up by Quasar/Vendor volume handler"};
}

Json::Value CommandServer::processGetRecordedSound(const Json::Value& request, websocketpp::connection_hdl handler) {
    YIO_LOG_DEBUG("processGetRecordedSound");

    SoundCollector::ChannelSound channelSounds;
    auto result = soundCollector->getRecordedSound(channelSounds);
    if (!result) {
        return prepareResponseJson(result, "", COMMAND_NAME_GET_RECORDED_SOUND);
    }
    const auto channelName = request.get("channelName", audioConfig->getSpotterChannelName()).asString();
    const auto channelToSoundIt = channelSounds.find(channelName);
    if (channelToSoundIt != channelSounds.end()) {
        // send header
        int size = 0;
        for (const auto& soundBuffer : channelToSoundIt->second) {
            size += soundBuffer->getData().size();
        }
        const auto soundInfo = channelToSoundIt->second.front()->getInfo();

        Json::Value header;
        header["result"] = true;
        header["command"] = COMMAND_NAME_GET_RECORDED_SOUND;
        header["channelName"] = channelName;
        header["soundInfo"]["size"] = size;
        header["soundInfo"]["sampleRate"] = soundInfo.getSampleRate();
        header["soundInfo"]["sampleSize"] = soundInfo.getSampleSize();
        header["soundInfo"]["channelCount"] = soundInfo.getChannelCount();
        header["refMessageId"] = request.get("messageId", "unspecified").asString();
        endpointSend(handler, header);

        // send data
        for (auto soundBuffer : channelToSoundIt->second) {
            websocketpp::lib::error_code ec;
            endpoint->send(handler,
                           soundBuffer->getData().data(),
                           soundBuffer->getData().size(),
                           websocketpp::frame::opcode::binary,
                           ec);
            if (ec) {
                YIO_LOG_ERROR_EVENT("CommandServer.SendRecordedSoundFailed", ec.message());
            }
        }

        return Json::Value();
    }

    return prepareResponseJson({false, channelName + " channel not found"}, "", COMMAND_NAME_GET_RECORDED_SOUND);
}

Json::Value CommandServer::processGetAudioConfig() {
    YIO_LOG_DEBUG("GET AUDIO CONFIG");
    auto responseJson = prepareResponseJson({}, "", COMMAND_NAME_GET_AUDIO_CONFIG);

    const auto& availableChannelNames = quasarAudioSourceAdapter->getAvailableChannelNames();
    for (const auto& channelName : availableChannelNames) {
        responseJson["extra"]["availableChannelNames"].append(channelName);
    }

    auto dumpingChannels = soundCollector->getDumpingChannels();
    if (dumpingChannels.empty()) {
        dumpingChannels = audioConfig->getDumpingChannelNames();
    }
    for (const auto& channelName : dumpingChannels) {
        responseJson["extra"]["dumpingChannelNames"].append(channelName);
    }

    return responseJson;
}

CommandResult CommandServer::setDumpingChannels(const std::unordered_set<std::string>& dumpingChannels) {
    const auto& availableChannelNames = quasarAudioSourceAdapter->getAvailableChannelNames();

    for (const auto& dumpingChannel : dumpingChannels) {
        if (availableChannelNames.count(dumpingChannel) == 0) {
            std::stringstream errorStream;
            errorStream << "Failed to start recording: there is no [" << dumpingChannel << "] in available channels: ";
            for (const auto& availableChannelName : availableChannelNames) {
                errorStream << "[" << availableChannelName << "]";
            }
            YIO_LOG_ERROR_EVENT("CommandServer.InvalidDumpingChannels", errorStream.str());
            return {false, errorStream.str()};
        }
    }

    soundCollector->setDumpingChannels(dumpingChannels);
    return {};
}

Json::Value CommandServer::processSubscribeLogs(const Json::Value& request, websocketpp::connection_hdl handler) {
    YIO_LOG_DEBUG("SUBSCRIBE LOGS");
    if (!request.isMember("logLevel")) {
        return prepareResponseJson({false, "No log level in request"}, "", COMMAND_NAME_SUBSCRIBE_LOGS);
    }

    const auto& logLevel = request["logLevel"];
    if (logLevel == "error" || logLevel == "info" || logLevel == "debug") {
        quasar::Logging::changeLoggerLevel(logLevel.asString());
        YandexIO::setSpeechkitLogLevel(logLevel.asString());
    } else if (logLevel == "verbose") {
        quasar::Logging::changeLoggerLevel("trace");
        YandexIO::setSpeechkitLogLevel("trace");
    } else {
        return prepareResponseJson({false, "Unsupported log level"}, "", COMMAND_NAME_SUBSCRIBE_LOGS);
    }

    auto addressAndPort = endpoint->get_con_from_hdl(handler)->get_remote_endpoint();
    LogStreamer::getInstance()->setServerAddress(addressAndPort.substr(0, addressAndPort.find_last_of(':')));
    LogStreamer::getInstance()->start();
    return prepareResponseJson({}, "", COMMAND_NAME_SUBSCRIBE_LOGS);
}

Json::Value CommandServer::processUnsubscribeLogs() {
    YIO_LOG_DEBUG("UNSUBSCRIBE LOGS");
    LogStreamer::getInstance()->stop();
    return prepareResponseJson({}, "", COMMAND_NAME_UNSUBSCRIBE_LOGS);
}

Json::Value CommandServer::processGetConfig() {
    YIO_LOG_DEBUG("GET CONFIG");

    auto responseJson = prepareResponseJson({}, "", COMMAND_NAME_GET_CONFIG);
    Json::Value& extra = responseJson["extra"];
    extra["config"] = config;

    return responseJson;
}

Json::Value CommandServer::processSetConfig(const Json::Value& request) {
    YIO_LOG_DEBUG("SET CONFIG");

    Json::Value responseJson;
    if (!validateConfig(request)) {
        responseJson = prepareResponseJson({false, "Invalid config"}, "", COMMAND_NAME_SET_CONFIG);
    } else {
        std::ofstream outfile(runtimeConfigPath);
        if (!outfile.good()) {
            responseJson = prepareResponseJson({false, "Cannot open " + runtimeConfigPath}, "", COMMAND_NAME_SET_CONFIG);
        } else {
            Json::FastWriter writer;
            outfile << writer.write(request["config"]);
            responseJson = prepareResponseJson({}, "", COMMAND_NAME_SET_CONFIG);
            outfile.close();
        }
    }

    return responseJson;
}

Json::Value CommandServer::processStartStreaming(const Json::Value& request, websocketpp::connection_hdl handler) {
    YIO_LOG_DEBUG("START STREAMING");

    if (state != State::IDLE) {
        if (state != State::STREAMING) {
            return prepareResponseJson({false, "Audiosender is not in idle state"}, "", COMMAND_NAME_START_STREAMING);
        }

        streamSoundCollector->stopRecording();
    }

    auto dumpingChannels = audioConfig->getDumpingChannelNames();
    soundCollector->disable();
    auto result = streamSoundCollector->startRecording(handler, request, dumpingChannels);
    if (!result) {
        return prepareResponseJson(result, "", COMMAND_NAME_START_STREAMING);
    }

    state = State::STREAMING;
    return {};
}

Json::Value CommandServer::processStopStreaming() {
    YIO_LOG_DEBUG("STOP STREAMING");

    if (state != State::STREAMING) {
        return prepareResponseJson({}, "", COMMAND_NAME_STOP_STREAMING);
    }

    streamSoundCollector->stopRecording();
    soundCollector->enable();
    state = State::IDLE;

    return prepareResponseJson({}, "", COMMAND_NAME_STOP_STREAMING);
}

void CommandServer::processRestart(websocketpp::connection_hdl /* handler */) {
    YIO_LOG_DEBUG("RESTART");

    std::system(rebootCommand.c_str());
}

void CommandServer::endpointSend(websocketpp::connection_hdl hdl, const Json::Value& payload) {
    std::string stringPayload = Json::FastWriter().write(payload);
    YIO_LOG_DEBUG("Sending response. Payload: " << stringPayload);
    websocketpp::lib::error_code ec;
    endpoint->send(hdl, stringPayload, websocketpp::frame::opcode::text, ec);
    if (ec) {
        YIO_LOG_ERROR_EVENT("CommandServer.SendMessageFailed", ec.message());
    }
}
