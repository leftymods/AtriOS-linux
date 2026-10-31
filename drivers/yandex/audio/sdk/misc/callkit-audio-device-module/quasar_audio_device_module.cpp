#include "quasar_audio_device_module.h"

#include <yandex_io/libs/audio_player/gstreamer/gstreamer.h>
#include <yandex_io/libs/audio_player/gstreamer/gstreamer_audio_player.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/ipc/i_connector.h>
#include <yandex_io/libs/ipc/datacratic/public.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/protos/quasar_proto.pb.h>

#include <contrib/libs/webrtc/rtc_base/ref_counted_object.h>

#include <future>
#include <thread>

YIO_DEFINE_LOG_MODULE("callkit");

using namespace quasar;

namespace {

    class WebrtcAppSrc: public quasar::StreamSrc {
    public:
        WebrtcAppSrc(
            quasar::QuasarAudioDeviceModule* adm,
            const std::string& name,
            int sampleRate = 16000,
            int channels = 1,
            int sampleSize = 2)
            : adm_(adm)
            , name_(name)
            , sampleRate_(sampleRate)
            , channels_(channels)
            , sampleSize_(sampleSize)
            , samplesIn10Ms_(sampleRate / 100)
            , bytesIn10Ms_(samplesIn10Ms_ * sampleSize_)
        {
            setSampleRate(sampleRate);
        }

        std::string getName() const override {
            return name_;
        }

        std::vector<uint8_t> pullData() override {
            std::vector<uint8_t> result(bytesIn10Ms_, 0);

            size_t nSamplesOut = 0;
            int64_t elapsedTimeMs;
            int64_t ntpTimeMs;

            adm_->audioTransport()->NeedMorePlayData(
                samplesIn10Ms_,
                sampleSize_,
                channels_,
                sampleRate_,
                result.data(),
                nSamplesOut,
                &elapsedTimeMs,
                &ntpTimeMs);

            return result;
        }

    private:
        quasar::QuasarAudioDeviceModule* adm_;

        const std::string name_;
        const int sampleRate_;
        const int channels_;
        const int sampleSize_;
        const int samplesIn10Ms_;
        const int bytesIn10Ms_;
    };

    void updateAudioOptions(cricket::AudioOptions& opts, const Json::Value& cfg) {
        if (cfg.isMember("echo_cancellation")) {
            opts.echo_cancellation = quasar::getBool(cfg, "echo_cancellation");
        }

        if (cfg.isMember("auto_gain_control")) {
            opts.auto_gain_control = quasar::getBool(cfg, "auto_gain_control");
        }

        if (cfg.isMember("noise_suppression")) {
            opts.noise_suppression = quasar::getBool(cfg, "noise_suppression");
        }

        if (cfg.isMember("highpass_filter")) {
            opts.highpass_filter = quasar::getBool(cfg, "highpass_filter");
        }

        if (cfg.isMember("stereo_swapping")) {
            opts.stereo_swapping = quasar::getBool(cfg, "stereo_swapping");
        }

        if (cfg.isMember("audio_jitter_buffer_max_packets")) {
            opts.audio_jitter_buffer_max_packets = quasar::getInt(cfg, "audio_jitter_buffer_max_packets");
        }

        if (cfg.isMember("audio_jitter_buffer_fast_accelerate")) {
            opts.audio_jitter_buffer_fast_accelerate = quasar::getBool(cfg, "audio_jitter_buffer_fast_accelerate");
        }

        if (cfg.isMember("audio_jitter_buffer_min_delay_ms")) {
            opts.audio_jitter_buffer_min_delay_ms = quasar::getInt(cfg, "audio_jitter_buffer_min_delay_ms");
        }

        if (cfg.isMember("audio_jitter_buffer_enable_rtx_handling")) {
            opts.audio_jitter_buffer_enable_rtx_handling = quasar::getBool(cfg, "audio_jitter_buffer_enable_rtx_handling");
        }

        if (cfg.isMember("typing_detection")) {
            opts.typing_detection = quasar::getBool(cfg, "typing_detection");
        }

        if (cfg.isMember("experimental_agc")) {
            opts.experimental_agc = quasar::getBool(cfg, "experimental_agc");
        }

        if (cfg.isMember("experimental_ns")) {
            opts.experimental_ns = quasar::getBool(cfg, "experimental_ns");
        }

        if (cfg.isMember("residual_echo_detector")) {
            opts.residual_echo_detector = quasar::getBool(cfg, "residual_echo_detector");
        }

        if (cfg.isMember("tx_agc_target_dbov")) {
            opts.tx_agc_target_dbov = quasar::getUInt32(cfg, "tx_agc_target_dbov");
        }

        if (cfg.isMember("tx_agc_digital_compression_gain")) {
            opts.tx_agc_digital_compression_gain = quasar::getUInt32(cfg, "tx_agc_digital_compression_gain");
        }

        if (cfg.isMember("tx_agc_limiter")) {
            opts.tx_agc_limiter = quasar::getBool(cfg, "tx_agc_limiter");
        }

        if (cfg.isMember("combined_audio_video_bwe")) {
            opts.combined_audio_video_bwe = quasar::getBool(cfg, "combined_audio_video_bwe");
        }

        if (cfg.isMember("audio_network_adaptor")) {
            opts.audio_network_adaptor = quasar::getBool(cfg, "audio_network_adaptor");
        }

        if (cfg.isMember("audio_network_adaptor_config")) {
            opts.audio_network_adaptor_config = quasar::getString(cfg, "audio_network_adaptor_config");
        }
    }

    void updateAudioProcessingConfig(webrtc::AudioProcessing::Config& cfg, const Json::Value& jsonCfg) {
        if (const auto preAmplifier = jsonCfg["pre_amplifier"]; !preAmplifier.isNull() && preAmplifier.isObject()) {
            if (const auto& enabled = preAmplifier["enabled"]; !enabled.isNull() && enabled.isBool()) {
                cfg.pre_amplifier.enabled = enabled.asBool();
            }

            if (const auto& factor = preAmplifier["fixed_gain_factor"]; !factor.isNull() && factor.isDouble()) {
                cfg.pre_amplifier.fixed_gain_factor = factor.asDouble();
            }
        }
    }

    YandexIO::RequestChannelType channelTypeFromString(const std::string& type) {
        if (type == "main") {
            return YandexIO::RequestChannelType::MAIN;

        } else if (type == "vqe") {
            return YandexIO::RequestChannelType::VQE;

        } else if (type == "raw") {
            return YandexIO::RequestChannelType::RAW;

        } else {
            throw std::runtime_error("Unexpected channelType: " + type);
        }
    }

} // namespace

void QuasarAudioDeviceModule::initMainChannelAndAudioOptions(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory) {
    Json::Value dynamicConfig;
    {
        std::promise<Json::Value> promise;

        auto toSyncd = ipcFactory->createIpcConnector("syncd");
        toSyncd->setMessageHandler([&promise](const auto& message) {
            if (message->has_user_config_update() && message->user_config_update().has_config()) {
                try {
                    promise.set_value(parseJson(message->user_config_update().config())["system_config"]["calld"]);

                } catch (const std::future_error&) {
                }
            }
        });

        toSyncd->connectToService();

        auto future = promise.get_future();

        if (future.wait_for(std::chrono::seconds(10)) == std::future_status::ready) {
            dynamicConfig = future.get();

        } else {
            YIO_LOG_ERROR_EVENT("QuasarAudioDeviceModule.GetQuasmodromConfigTimeout", "Can't get quasmodrom config");
        }
    }

    Channel c;
    c.name = tryGetString(defaultConfig_, "inputChannelName", "vqe");
    c.type = channelTypeFromString(tryGetString(defaultConfig_, "inputChannelType", "vqe"));
    c.channels = tryGetInt(defaultConfig_, "inputChannelChannels", Channel::DEFAULT_CHANNELS);
    c.sampleRate = tryGetInt(defaultConfig_, "inputChannelSampleRate", Channel::DEFAULT_SAMPLE_RATE);
    c.sampleSize = tryGetInt(defaultConfig_, "inputChannelSampleSize", Channel::DEFAULT_SAMPLE_SIZE);

    if (!dynamicConfig.isNull()) {
        c.name = tryGetString(dynamicConfig, "inputChannelName", c.name);
        if (dynamicConfig.isMember("inputChannelType")) {
            c.type = channelTypeFromString(getString(dynamicConfig, "inputChannelType"));
        }
        c.channels = tryGetInt(dynamicConfig, "inputChannelChannels", c.channels);
        c.sampleRate = tryGetInt(dynamicConfig, "inputChannelSampleRate", c.sampleRate);
        c.sampleSize = tryGetInt(dynamicConfig, "inputChannelSampleSize", c.sampleSize);
    }

    mainChannel_ = c;

    YIO_LOG_INFO(
        "QuasarAudioDeviceModule::mainChannel: name="
        << mainChannel_.name
        << ", type="
        << (int)mainChannel_.type
        << ", channels="
        << mainChannel_.channels
        << ", sampleRate="
        << mainChannel_.sampleRate
        << ", sampleSize="
        << mainChannel_.sampleSize);

    cricket::AudioOptions opts;

    updateAudioOptions(opts, defaultConfig_);

    if (!dynamicConfig.isNull()) {
        updateAudioOptions(opts, dynamicConfig);
    }

    audioOptions_ = opts;

    YIO_LOG_INFO("QuasarAudioDeviceModule::audioOptions: " << audioOptions_.ToString());

    webrtc::AudioProcessing::Config apConfig;

    if (!defaultConfig_["audio_processing_config"].isNull()) {
        updateAudioProcessingConfig(apConfig, defaultConfig_["audio_processing_config"]);
    }

    if (!dynamicConfig["audio_processing_config"].isNull()) {
        updateAudioProcessingConfig(apConfig, dynamicConfig["audio_processing_config"]);
    }

    audioProcessingConfig_ = apConfig;

    YIO_LOG_INFO("QuasarAudioDeviceModule::audioProcessingConfig: " << audioProcessingConfig_.ToString());
}

QuasarAudioDeviceModule::QuasarAudioDeviceModule(
    const std::shared_ptr<ipc::IIpcFactory>& ipcFactory,
    std::shared_ptr<IUserConfigProvider> userConfigProvider,
    Json::Value callsConfig)
    : defaultConfig_(std::move(callsConfig))
    , userConfigProvider_(std::move(userConfigProvider))
{
    initMainChannelAndAudioOptions(ipcFactory);

    audioBuffer_.resize(mainChannel_.getBytesIn10Ms());

    ioAudioSource_ = YandexIO::createAudioSourceClient(ipcFactory);

    ioAudioSourceListener_ = std::make_shared<YandexIO::AudioSourceCallbackListener>([this](const YandexIO::ChannelsData& data) {
        auto findChannel = [requestType = mainChannel_.type](const YandexIO::ChannelData& channel) -> bool {
            switch (requestType) {
                case YandexIO::RequestChannelType::MAIN:
                    return channel.isForRecognition;

                case YandexIO::RequestChannelType::VQE:
                    return channel.type == YandexIO::ChannelData::Type::VQE;

                case YandexIO::RequestChannelType::RAW:
                    return channel.type == YandexIO::ChannelData::Type::RAW;

                default:
                    YIO_LOG_DEBUG("Unexpected requestChannelType: " << (int)requestType);
                    return false;
            }
        };

        const auto channel = std::find_if(data.cbegin(), data.cend(), findChannel);

        if (channel == data.cend()) {
            return;
        }

        handleAudioInput(*channel);
    });

    ioAudioSource_->addListener(ioAudioSourceListener_);
    ioAudioSource_->start();

    initializePlayer(defaultConfig_);
}

void QuasarAudioDeviceModule::actualizePlayer(
    const Json::Value& calldConfig,
    const Json::Value& sinkOptions) {
    if (playing_) {
        return;
    }
    quasar::gstreamer::GstreamerAudioPlayerFactory factory(quasar::gstreamer::ensureGstreamerInitialized());
    auto params = factory.createParams();
    params->setSinkOptionsJson(sinkOptions);
    params->setGstreamerPipeline(getString(calldConfig, "gstPipeline"));
    params->setStreamSrc(std::make_shared<WebrtcAppSrc>(this, getString(calldConfig, "appsrcElementName"),
                                                        getInt(calldConfig, "appsrcSampleRate")));
    player_ = factory.createPlayer(*params);
}

void QuasarAudioDeviceModule::initializePlayer(
    const Json::Value& calldConfig) {
    actualizePlayer(calldConfig);
    userConfigProvider_
        ->jsonChangedSignal(IUserConfigProvider::ConfigScope::SYSTEM, "calldSinkOptions")
        .connect(
            [this, calldConfig](const auto& calldSinkOptionsJson) {
                actualizePlayer(calldConfig, *calldSinkOptionsJson);
            },
            preparePlayerLifetime_);
}

QuasarAudioDeviceModule::~QuasarAudioDeviceModule() {
    Terminate();
}

rtc::scoped_refptr<QuasarAudioDeviceModule> QuasarAudioDeviceModule::create(
    const std::shared_ptr<ipc::IIpcFactory>& ipcFactory,
    std::shared_ptr<IUserConfigProvider> userConfigProvider,
    Json::Value callsConfig) {
    return new rtc::RefCountedObject<QuasarAudioDeviceModule>(ipcFactory, std::move(userConfigProvider), std::move(callsConfig));
}

int32_t QuasarAudioDeviceModule::ActiveAudioLayer(AudioLayer* /*audioLayer*/) const {
    return -1;
}

int32_t QuasarAudioDeviceModule::RegisterAudioCallback(webrtc::AudioTransport* audioCallback) {
    std::lock_guard<std::mutex> g(lock_);

    if (playing_ || recording_) {
        YIO_LOG_ERROR_EVENT("QuasarAudioDeviceModule.RegisterAudioCallback.AlreadyActive", "Can't set audio callback: media is active");

        return -1;
    }

    audioCallback_ = audioCallback;

    return 0;
}

int32_t QuasarAudioDeviceModule::Init() {
    std::lock_guard<std::mutex> g(lock_);

    initialized_ = true;

    return 0;
}

int32_t QuasarAudioDeviceModule::Terminate() {
    std::lock_guard<std::mutex> g(lock_);

    if (playing_) {
        StopPlayoutNoLock();
    }

    if (recording_) {
        StopRecordingNoLock();
    }

    initialized_ = false;

    return 0;
}

bool QuasarAudioDeviceModule::Initialized() const {
    std::lock_guard<std::mutex> g(lock_);

    return initialized_;
}

int16_t QuasarAudioDeviceModule::PlayoutDevices() {
    return -1;
}

int16_t QuasarAudioDeviceModule::RecordingDevices() {
    return -1;
}

int32_t QuasarAudioDeviceModule::PlayoutDeviceName(
    uint16_t /*index*/,
    char /*name*/[webrtc::kAdmMaxDeviceNameSize],
    char /*guid*/[webrtc::kAdmMaxGuidSize])
{
    return -1;
}

int32_t QuasarAudioDeviceModule::RecordingDeviceName(
    uint16_t /*index*/,
    char /*name*/[webrtc::kAdmMaxDeviceNameSize],
    char /*guid*/[webrtc::kAdmMaxGuidSize])
{
    return -1;
}

int32_t QuasarAudioDeviceModule::SetPlayoutDevice(uint16_t /*index*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::SetPlayoutDevice(WindowsDeviceType /*device*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::SetRecordingDevice(uint16_t /*index*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::SetRecordingDevice(WindowsDeviceType /*device*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::PlayoutIsAvailable(bool* /*available*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::InitPlayout() {
    std::lock_guard<std::mutex> g(lock_);

    playoutInitialized_ = true;

    return 0;
}

bool QuasarAudioDeviceModule::PlayoutIsInitialized() const {
    std::lock_guard<std::mutex> g(lock_);

    return playoutInitialized_;
}

int32_t QuasarAudioDeviceModule::RecordingIsAvailable(bool* /*available*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::InitRecording() {
    std::lock_guard<std::mutex> g(lock_);

    recordingInitialized_ = true;

    return 0;
}

bool QuasarAudioDeviceModule::RecordingIsInitialized() const {
    std::lock_guard<std::mutex> g(lock_);

    return recordingInitialized_;
}

int32_t QuasarAudioDeviceModule::StartPlayout() {
    std::lock_guard<std::mutex> g(lock_);

    if (!playoutInitialized_) {
        return -1;
    }

    if (playing_) {
        return 0;
    }

    if (!audioCallback_) {
        return -1;
    }

    preparePlayerLifetime_.die();
    playing_ = true;

    player_->playAsync();

    return 0;
}

int32_t QuasarAudioDeviceModule::StopPlayout() {
    std::lock_guard<std::mutex> g(lock_);

    StopPlayoutNoLock();

    return 0;
}

int32_t QuasarAudioDeviceModule::StopPlayoutNoLock() {
    if (!playoutInitialized_) {
        return 0;
    }

    if (!playing_) {
        return 0;
    }

    playing_ = false;

    player_->stop();

    return 0;
}

bool QuasarAudioDeviceModule::Playing() const {
    std::lock_guard<std::mutex> g(lock_);

    return playing_;
}

int32_t QuasarAudioDeviceModule::StartRecording() {
    std::lock_guard<std::mutex> g(lock_);

    if (!recordingInitialized_) {
        return -1;
    }

    if (recording_) {
        return 0;
    }

    if (!audioCallback_) {
        return -1;
    }

    recording_ = true;

    ioAudioSource_->subscribeToChannels(mainChannel_.type);

    return 0;
}

int32_t QuasarAudioDeviceModule::StopRecording() {
    std::lock_guard<std::mutex> g(lock_);

    StopRecordingNoLock();

    return 0;
}

int32_t QuasarAudioDeviceModule::StopRecordingNoLock() {
    if (!recordingInitialized_) {
        return 0;
    }

    recording_ = false;

    ioAudioSource_->unsubscribeFromChannels();

    return 0;
}

bool QuasarAudioDeviceModule::Recording() const {
    std::lock_guard<std::mutex> g(lock_);

    return recording_;
}

int32_t QuasarAudioDeviceModule::InitSpeaker() {
    return -1;
}

bool QuasarAudioDeviceModule::SpeakerIsInitialized() const {
    return false;
}

int32_t QuasarAudioDeviceModule::InitMicrophone() {
    return -1;
}

bool QuasarAudioDeviceModule::MicrophoneIsInitialized() const {
    return false;
}

int32_t QuasarAudioDeviceModule::SpeakerVolumeIsAvailable(bool* /*available*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::SetSpeakerVolume(uint32_t /*volume*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::SpeakerVolume(uint32_t* /*volume*/) const {
    return -1;
}

int32_t QuasarAudioDeviceModule::MaxSpeakerVolume(uint32_t* /*maxVolume*/) const {
    return -1;
}

int32_t QuasarAudioDeviceModule::MinSpeakerVolume(uint32_t* /*minVolume*/) const {
    return -1;
}

int32_t QuasarAudioDeviceModule::MicrophoneVolumeIsAvailable(bool* /*available*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::SetMicrophoneVolume(uint32_t /*volume*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::MicrophoneVolume(uint32_t* /*volume*/) const {
    return -1;
}

int32_t QuasarAudioDeviceModule::MaxMicrophoneVolume(uint32_t* /*maxVolume*/) const {
    return -1;
}

int32_t QuasarAudioDeviceModule::MinMicrophoneVolume(uint32_t* /*minVolume*/) const {
    return -1;
}

int32_t QuasarAudioDeviceModule::SpeakerMuteIsAvailable(bool* /*available*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::SetSpeakerMute(bool /*enable*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::SpeakerMute(bool* /*enabled*/) const {
    return -1;
}

int32_t QuasarAudioDeviceModule::MicrophoneMuteIsAvailable(bool* /*available*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::SetMicrophoneMute(bool /*enable*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::MicrophoneMute(bool* /*enabled*/) const {
    return -1;
}

int32_t QuasarAudioDeviceModule::StereoPlayoutIsAvailable(bool* /*available*/) const {
    return -1;
}

int32_t QuasarAudioDeviceModule::SetStereoPlayout(bool /*enable*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::StereoPlayout(bool* /*enabled*/) const {
    return -1;
}

int32_t QuasarAudioDeviceModule::StereoRecordingIsAvailable(bool* /*available*/) const {
    return -1;
}

int32_t QuasarAudioDeviceModule::SetStereoRecording(bool /*enable*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::StereoRecording(bool* /*enabled*/) const {
    return -1;
}

int32_t QuasarAudioDeviceModule::PlayoutDelay(uint16_t* delay_ms) const {
    // TODO: snd_pcm_delay
    *delay_ms = 0;
    return 0;
}

bool QuasarAudioDeviceModule::BuiltInAECIsAvailable() const {
    return false;
}

bool QuasarAudioDeviceModule::BuiltInAGCIsAvailable() const {
    return false;
}

bool QuasarAudioDeviceModule::BuiltInNSIsAvailable() const {
    return false;
}

int32_t QuasarAudioDeviceModule::EnableBuiltInAEC(bool /*enable*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::EnableBuiltInAGC(bool /*enable*/) {
    return -1;
}

int32_t QuasarAudioDeviceModule::EnableBuiltInNS(bool /*enable*/) {
    return -1;
}

webrtc::AudioTransport* QuasarAudioDeviceModule::audioTransport() {
    std::lock_guard<std::mutex> g(lock_);

    return audioCallback_;
}

void QuasarAudioDeviceModule::handleAudioInput(const YandexIO::ChannelData& channelData) {
    Y_VERIFY(audioBufferEnd_ < mainChannel_.getBytesIn10Ms());

    const auto rawPtr = reinterpret_cast<const char*>(channelData.data.data());
    const auto size = channelData.data.size() * channelData.sampleSize;
    for (size_t dataPos = 0, dataSize = size; dataPos < dataSize;) {
        const size_t srcSize = dataSize - dataPos;
        const size_t dstSize = mainChannel_.getBytesIn10Ms() - audioBufferEnd_;
        const size_t cpySize = std::min(srcSize, dstSize);

        memcpy(audioBuffer_.data() + audioBufferEnd_, rawPtr + dataPos, cpySize);

        audioBufferEnd_ += cpySize;
        dataPos += cpySize;

        if (audioBufferEnd_ == mainChannel_.getBytesIn10Ms()) {
            uint32_t volume;

            std::lock_guard<std::mutex> g(lock_);

            if (!recording_ || !audioCallback_) {
                // drop chunks until recording
                audioBufferEnd_ = 0;
                return;
            }

            const int res = audioCallback_->RecordedDataIsAvailable(
                audioBuffer_.data(),
                mainChannel_.getSamplesIn10Ms(),
                mainChannel_.sampleSize,
                mainChannel_.channels,
                mainChannel_.sampleRate,
                0,
                0,
                0,
                false,
                volume);

            if (res == -1) {
                YIO_LOG_ERROR_EVENT("QuasarAudioDeviceModule.PushAudioDataFail", "Can't put audio data to webrtc: RecordedDataIsAvailable() returned " << res);
            }

            audioBufferEnd_ = 0;
        }
    }
}

cricket::AudioOptions QuasarAudioDeviceModule::getAudioOptions() const {
    return audioOptions_;
}

webrtc::AudioProcessing::Config QuasarAudioDeviceModule::getAudioProcessingConfig() const {
    return audioProcessingConfig_;
}
