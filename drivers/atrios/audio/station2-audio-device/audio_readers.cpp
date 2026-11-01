#include "audio_readers.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <util/system/yassert.h>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

namespace {
    constexpr int MICS_COUNT = 7;
    constexpr auto DEFAULT_READ_SAMPLE_RATE = 48000;
    constexpr auto VQE_INPUT_RATE = 16000;
    constexpr auto SAMPLE_SIZE = 2;
    /* N.B.: alsa device with hardware sync of feedback and mic data has 16 channels
     * [0..7] -- mics
     * [10..11] - spk
     * other -- junk
     */
    constexpr auto HW_SYNC_DEVICE_CHANNELS = 16;

    /**
     * Hw mapping doesn't match expected mics mapping. So reorder mics. 8 mic should be discarded
     * Mapping (note: mapping starts from 1 up to 8. array mapping will starts from 0):
     */
    constexpr std::array<short, MICS_COUNT> MICS_REORDER_MAP{
        3, // 1 -> 4
        4, // 2 -> 5
        5, // 3 -> 6
        0, // 4 -> 1
        1, // 5 -> 2
        2, // 6 -> 3
        6, // 7 -> 7
           // 8 -> discarded
    };

    quasar::AudioReaderConfig prepareMicsAudioReaderConfig(quasar::AudioReaderConfig config, bool useHwSync) {
        if (useHwSync) {
            // Custom create function to create TinyAlsaDevice with 16 channels
            config.spkChannels = 0;
            config.micChannels = HW_SYNC_DEVICE_CHANNELS;
        }
        return config;
    }

    std::unique_ptr<quasar::TinyAlsaAudioReader> createTinyAlsaAudioReader(const quasar::AudioReaderConfig& config) {
        if (config.sampleSize != SAMPLE_SIZE) {
            throw std::runtime_error("Unknown sample size");
        }
        auto audioReader = std::make_unique<quasar::TinyAlsaAudioReader>();
        audioReader->open(config.cardNumber, config.deviceNumber, config.micChannels + config.spkChannels,
                          config.inRate, PCM_FORMAT_S16_LE, config.periodSize, config.periodCount);
        return audioReader;
    }
} // namespace

namespace quasar {

    IYandexStation2AudioReader::IYandexStation2AudioReader(int channelsUsed, AudioReaderConfig config)
        : channelsUsed_(channelsUsed)
        , config_(std::move(config))
        , buffer_()
    {
    }

    IYandexStation2AudioReader::IYandexStation2AudioReader(int channelsUsed, AudioReaderConfig config, size_t bufferSize)
        : channelsUsed_(channelsUsed)
        , config_(std::move(config))
        , buffer_(bufferSize)
    {
    }

    IYandexStation2AudioReader::~IYandexStation2AudioReader() = default;

    int IYandexStation2AudioReader::channelsUsed() const {
        return channelsUsed_;
    }

    const AudioReaderConfig& IYandexStation2AudioReader::config() const {
        return config_;
    }

    const AudioBuffer& IYandexStation2AudioReader::buffer() const {
        return buffer_;
    }

    ZerosAudioReader::ZerosAudioReader(int channelsUsed, size_t bufferSize)
        : IYandexStation2AudioReader(
              channelsUsed,
              AudioReaderConfig(Json::Value::null, ""),
              bufferSize)
        , downsampled_(bufferSize)
    {
    }

    void ZerosAudioReader::readDataOrThrow(std::shared_ptr<YandexIO::AudioDeviceStats::ChunkSession> /* session */, YandexIO::AudioDeviceStats::Tag /*statsTag*/) {
    }

    void ZerosAudioReader::read() {
    }

    const Downsampler::OutputVectorType& ZerosAudioReader::downsample(const Downsampler::InputVectorType& /* in */) {
        return downsampled_;
    }

    void ZerosAudioReader::close() {
    }

    void ZerosAudioReader::start() {
    }

    void ZerosAudioReader::stop() {
    }

    void ZerosAudioReader::tryRecover() {
    }

    bool ZerosAudioReader::wait(std::chrono::milliseconds /* timeout */) {
        return true;
    }

    unsigned int ZerosAudioReader::getBufferSize() {
        return buffer_.size();
    }

    YandexStation2AudioReader::YandexStation2AudioReader(
        AudioReaderConfig config,
        std::unique_ptr<TinyAlsaAudioReader> reader,
        int channelsUsed,
        Downsampler downsampler,
        std::string_view logTag)
        : IYandexStation2AudioReader(channelsUsed, std::move(config))
        , reader_(std::move(reader))
        , downsampler_(std::move(downsampler))
        , logTag_(logTag)
    {
    }

    IYandexStation2AudioReader::Ptr YandexStation2AudioReader::createSpk(const Json::Value& config, const std::string& deviceType, const vqe::DigitalFilterConfig& vqeConfig, std::string_view logTag) {
        AudioReaderConfig audioConfig{config, deviceType};
        const int channelsUsed = audioConfig.spkChannels;
        return std::make_unique<YandexStation2AudioReader>(
            std::move(audioConfig),
            createTinyAlsaAudioReader(audioConfig),
            channelsUsed,
            Downsampler(audioConfig.inRate, VQE_INPUT_RATE, channelsUsed, vqeConfig),
            logTag);
    }

    IYandexStation2AudioReader::Ptr YandexStation2AudioReader::createMic(const Json::Value& config, const std::string& deviceType, const vqe::DigitalFilterConfig& vqeConfig, std::string_view logTag, bool useHwSync) {
        AudioReaderConfig audioConfig{config, deviceType};
        auto micConfig = prepareMicsAudioReaderConfig(audioConfig, useHwSync);
        const int channelsUsed = audioConfig.micChannels - 1;
        return std::make_unique<YandexStation2AudioReader>(
            std::move(audioConfig),
            createTinyAlsaAudioReader(micConfig),
            channelsUsed,
            Downsampler(audioConfig.inRate, VQE_INPUT_RATE, MICS_REORDER_MAP.size(), vqeConfig),
            logTag);
    }

    YandexStation2AudioReader::~YandexStation2AudioReader() = default;

    void YandexStation2AudioReader::readDataOrThrow(std::shared_ptr<YandexIO::AudioDeviceStats::ChunkSession> logSession, YandexIO::AudioDeviceStats::Tag statsTag) {
        logSession->start(statsTag);
        const bool error = !reader_->read(buffer_, config_.periodSize);
        logSession->stop(statsTag);

        if (error) {
            std::stringstream ss;
            ss << "Read from " << logTag_ << " failed";
            throw std::runtime_error(ss.str());
        }
    }

    void YandexStation2AudioReader::read() {
        if (!reader_->read(buffer_, config_.periodSize)) {
            try {
                YIO_LOG_ERROR_EVENT("Station2AudioReader.ReadFailed", logTag_ << " read returned false; Error: " << reader_->getError() << ". Trying to recover");
                reader_->tryRecover();
            } catch (const std::runtime_error& e) {
                YIO_LOG_ERROR_EVENT("Station2AudioReader.Exception", logTag_ << " error with unsuccessful recover: " << e.what());
            }
        }
    }

    std::vector<float> convertToFloat(const IYandexStation2AudioReader& reader, const AudioBuffer& in) {
        const auto size = reader.config().periodSize;
        const auto channelsCount = reader.channelsUsed();
        const int16_t* inPtr = reinterpret_cast<const int16_t*>(in.data());

        std::vector<float> result;
        result.resize(size * channelsCount);
        for (auto i = 0; i < size; ++i) {
            for (auto c = 0; c < channelsCount; ++c) {
                result[channelsCount * i + c] = static_cast<float>(inPtr[channelsCount * i + c]) / AudioReaders::SAMPLE_FLOAT_SCALE;
            }
        }
        return result;
    };

    std::vector<float> convertToFloatAndReorder(const IYandexStation2AudioReader& reader, const AudioBuffer& in) {
        // NB: Discard last channel (it duplicate 7th mic) and reorder mics channels (so DOA will work)
        const auto periodSize = reader.config().periodSize;
        const auto inChannels = reader.config().micChannels;
        const int outChannels = MICS_REORDER_MAP.size();
        if (Y_UNLIKELY(inChannels < outChannels)) {
            std::stringstream ss;
            ss << "Incorrect inChannels num. inChannels should be >= outChannels. In: " << inChannels << " out: " << outChannels;
            throw std::runtime_error(ss.str());
        }
        std::vector<float> out(periodSize * outChannels);
        const int16_t* inPtr = reinterpret_cast<const int16_t*>(in.data());
        for (auto i = 0; i < periodSize; ++i) {
            const auto inPeriodBegin = inChannels * i;
            const auto outPeriodBegin = outChannels * i;
            for (int c = 0; c < outChannels; ++c) {
                const auto reordered = MICS_REORDER_MAP[c];
                const auto sample = static_cast<float>(inPtr[inPeriodBegin + c]) / AudioReaders::SAMPLE_FLOAT_SCALE;
                out[outPeriodBegin + reordered] = sample;
            }
        }
        return out;
    };

    const Downsampler::OutputVectorType& YandexStation2AudioReader::downsample(const Downsampler::InputVectorType& in) {
        return downsampler_(in);
    }

    void YandexStation2AudioReader::close() {
        reader_->close();
    }

    void YandexStation2AudioReader::start() {
        reader_->start();
    }

    void YandexStation2AudioReader::stop() {
        reader_->stop();
    }

    void YandexStation2AudioReader::tryRecover() {
        reader_->tryRecover();
    }

    bool YandexStation2AudioReader::wait(std::chrono::milliseconds timeoutMs) {
        return reader_->wait(timeoutMs.count());
    }

    unsigned int YandexStation2AudioReader::getBufferSize() {
        return reader_->getBufferSize();
    }

    enum AudioReadersIdx {
        SPK,
        MIC,
        LOOPBACK,
        HDMI_LOOPBACK
    };

    AudioReaders::AudioReaders(Readers readers, bool useHwFeedbackSync, YandexIO::FeedbackSource hardwareSyncTarget)
        : useHwFeedbackToMicSync_(useHwFeedbackSync)
        , readers_(std::move(readers))
        , splitter_(YandexIO::IChannelsSplitter::create(SAMPLE_SIZE, HW_SYNC_DEVICE_CHANNELS))
        , hardwareSyncTarget_(hardwareSyncTarget)
    {
        for (const auto& reader : readers_) {
            Y_ASSERT(reader);
        }
    }

    AudioReaders::~AudioReaders() = default;

    AudioReaders::AudioReaders(AudioReaders&& audioReaders) noexcept
        : useHwFeedbackToMicSync_(audioReaders.useHwFeedbackToMicSync_)
        , readers_(std::move(audioReaders.readers_))
        , splitter_(std::move(audioReaders.splitter_))
        , hardwareSyncTarget_(audioReaders.hardwareSyncTarget_)
    {
    }

    void AudioReaders::stop() {
        for (auto& reader : readers_) {
            reader->stop();
        }
    }

    void AudioReaders::start() {
        for (auto& reader : readers_) {
            reader->start();
        }
    }

    void AudioReaders::waitDevicesReady() {
        for (auto& reader : readers_) {
            reader->wait(std::chrono::milliseconds(100));
        }
    }

    void AudioReaders::close() {
        for (auto& reader : readers_) {
            reader->close();
        }
    }

    void AudioReaders::tryRecover() {
        for (auto& reader : readers_) {
            reader->tryRecover();
        }
    }

    bool AudioReaders::read(AudioBuffer& micBuff, AudioBuffer& spkBuff, AudioBuffer& lbBuff, AudioBuffer& hdmiLbBuff,
                            std::shared_ptr<YandexIO::AudioDeviceStats::ChunkSession> logSession) {
        if (useHwFeedbackToMicSync_) {
            return !readWithHwSync(micBuff, spkBuff, lbBuff, hdmiLbBuff, logSession);
        } else {
            return !readWithoutHwSync(micBuff, spkBuff, lbBuff, hdmiLbBuff, logSession);
        }
    }

    void AudioReaders::checkAudioReaderConfigs() const {
        const auto& micReaderConfig = micAudioReader().config();
        const auto& spkReaderConfig = spkAudioReader().config();
        const auto& lbReaderConfig = lbAudioReader().config();
        if (micReaderConfig.inRate != DEFAULT_READ_SAMPLE_RATE) {
            throw std::runtime_error("Only 16k and 48k read rate are supported for mic reader now");
        }

        if (micReaderConfig.micChannels != 8 && micReaderConfig.micChannels != 16) {
            std::stringstream ss;
            ss << "8 or 16 channels are supported for mic reader now, tried channels " << micReaderConfig.micChannels;
            throw std::runtime_error(ss.str());
        }
        // TODO: reuse channels used
        if (MICS_REORDER_MAP.size() != micAudioReader().channelsUsed()) {
            std::stringstream ss;
            ss << "Invalid Mics Reorder Map. 1 channel should be discarded " << MICS_REORDER_MAP.size() << " == " << micAudioReader().channelsUsed();
            throw std::runtime_error(ss.str());
        }

        if (micReaderConfig.sampleSize != 2) {
            throw std::runtime_error("Only 2 byte sample size is supported for mic reader now");
        }

        if (spkReaderConfig.inRate != DEFAULT_READ_SAMPLE_RATE) {
            throw std::runtime_error("Only 48k read rate is supported for spk reader now");
        }

        if (spkReaderConfig.sampleSize != 2) {
            throw std::runtime_error("Only 2 byte sample size is supported for spk reader now");
        }

        if (spkAudioReader().channelsUsed() != 2) {
            throw std::runtime_error("Only 2 channels are supported for spk reader now");
        }

        if (lbReaderConfig.inRate != DEFAULT_READ_SAMPLE_RATE) {
            throw std::runtime_error("Only 48k read rate is supported for loopback reader now");
        }

        if (lbReaderConfig.sampleSize != 2) {
            throw std::runtime_error("Only 2 byte sample size is supported for loopback reader now");
        }

        if (lbAudioReader().channelsUsed() != 2) {
            throw std::runtime_error("Only 2 channels are supported for loopback reader now");
        }
    }

    bool AudioReaders::readWithoutHwSync(AudioBuffer& micBuff, AudioBuffer& spkBuff, AudioBuffer& lbBuff, AudioBuffer& hdmiLbBuff,
                                         std::shared_ptr<YandexIO::AudioDeviceStats::ChunkSession> logSession) {
        /* Capture feedback first. vqe require that feedback data is ahead of mic data, so read it's data first */
        logSession->start(YandexIO::AudioDeviceStats::Tag::READ_HW_SPK);
        spkAudioReader().read();
        logSession->stop(YandexIO::AudioDeviceStats::Tag::READ_HW_SPK);

        logSession->start(YandexIO::AudioDeviceStats::Tag::READ_SW_LB);
        lbAudioReader().read();
        logSession->stop(YandexIO::AudioDeviceStats::Tag::READ_SW_LB);

        logSession->start(YandexIO::AudioDeviceStats::Tag::READ_HW_MIC);
        micAudioReader().read();
        logSession->stop(YandexIO::AudioDeviceStats::Tag::READ_HW_MIC);

        logSession->start(YandexIO::AudioDeviceStats::Tag::READ_HDMI_LB);
        hdmiAudioReader().read();
        logSession->stop(YandexIO::AudioDeviceStats::Tag::READ_HDMI_LB);

        spkBuff = this->spkBuff();
        lbBuff = this->lbBuff();
        micBuff = this->micBuff();
        hdmiLbBuff = this->hdmiLbBuff();
        const bool hasEmpty = spkBuff.empty() || lbBuff.empty() || micBuff.empty() || hdmiLbBuff.empty();
        return !hasEmpty;
    }

    bool AudioReaders::readWithHwSync(AudioBuffer& micBuff, AudioBuffer& spkBuff, AudioBuffer& lbBuff, AudioBuffer& hdmiLbBuff,
                                      std::shared_ptr<YandexIO::AudioDeviceStats::ChunkSession> logSession) {
        try {
            // read data from audio device and drop it. Actual data will be written to "mics" alsa device data
            // this call can't be dropped, otherwise "mics" alsa device data will not contain spk channels
            spkAudioReader().readDataOrThrow(logSession, YandexIO::AudioDeviceStats::Tag::READ_HW_MIC_STUB);
            micAudioReader().readDataOrThrow(logSession, YandexIO::AudioDeviceStats::Tag::READ_HW_MIC_SPK);
            lbAudioReader().readDataOrThrow(logSession, YandexIO::AudioDeviceStats::Tag::READ_SW_LB);
            hdmiAudioReader().readDataOrThrow(logSession, YandexIO::AudioDeviceStats::Tag::READ_HDMI_LB);
        } catch (const std::runtime_error& e) {
            close();
            tryRecover();
            return false;
        }
        // extract target channels
        splitter_->extractChannels(this->micBuff(), {0, 1, 2, 3, 4, 5, 6, 7}, micBuff);
        if (hardwareSyncTarget_ == YandexIO::FeedbackSource::HW) {
            splitter_->extractChannels(this->micBuff(), {10, 11}, spkBuff);
            hdmiLbBuff = this->hdmiLbBuff();
            lbBuff = this->lbBuff();
        } else if (hardwareSyncTarget_ == YandexIO::FeedbackSource::HDMI) {
            splitter_->extractChannels(this->micBuff(), {8, 9}, hdmiLbBuff);
            spkBuff = this->spkBuff();
            lbBuff = this->lbBuff();
        } else if (hardwareSyncTarget_ == YandexIO::FeedbackSource::SW) {
            splitter_->extractChannels(this->micBuff(), {8, 9}, lbBuff);
            spkBuff = this->spkBuff();
            hdmiLbBuff = this->hdmiLbBuff();
        } else {
            throw std::runtime_error("Unknown sync source");
        }
        return true;
    }

    const std::vector<float>& downsample(IYandexStation2AudioReader& reader, const AudioBuffer& data) {
        auto floatData = convertToFloat(reader, data);
        return reader.downsample(floatData);
    }

    const std::vector<float>& AudioReaders::downsampleSpk(const AudioBuffer& data) {
        return downsample(spkAudioReader(), data);
    }

    const std::vector<float>& AudioReaders::downsampleMic(const AudioBuffer& data) {
        auto& reader = micAudioReader();
        auto floatData = convertToFloatAndReorder(reader, data);
        return reader.downsample(floatData);
    }

    const std::vector<float>& AudioReaders::downsampleLb(const AudioBuffer& data) {
        return downsample(lbAudioReader(), data);
    }

    const std::vector<float>& AudioReaders::downsampleHdmi(const AudioBuffer& data) {
        return downsample(hdmiAudioReader(), data);
    }

    int AudioReaders::micChannelsUsed() const {
        return micAudioReader().channelsUsed();
    }

    int AudioReaders::spkChannelsUsed() const {
        return spkAudioReader().channelsUsed();
    }

    int AudioReaders::lbChannelsUsed() const {
        return lbAudioReader().channelsUsed();
    }

    int AudioReaders::hdmiChannelsUsed() const {
        return hdmiAudioReader().channelsUsed();
    }

    const IYandexStation2AudioReader& AudioReaders::spkAudioReader() const {
        return *readers_[AudioReadersIdx::SPK];
    }

    const IYandexStation2AudioReader& AudioReaders::micAudioReader() const {
        return *readers_[AudioReadersIdx::MIC];
    }

    const IYandexStation2AudioReader& AudioReaders::lbAudioReader() const {
        return *readers_[AudioReadersIdx::LOOPBACK];
    }

    const IYandexStation2AudioReader& AudioReaders::hdmiAudioReader() const {
        return *readers_[AudioReadersIdx::HDMI_LOOPBACK];
    }

    IYandexStation2AudioReader& AudioReaders::spkAudioReader() {
        return *readers_[AudioReadersIdx::SPK];
    }

    IYandexStation2AudioReader& AudioReaders::micAudioReader() {
        return *readers_[AudioReadersIdx::MIC];
    }

    IYandexStation2AudioReader& AudioReaders::lbAudioReader() {
        return *readers_[AudioReadersIdx::LOOPBACK];
    }

    IYandexStation2AudioReader& AudioReaders::hdmiAudioReader() {
        return *readers_[AudioReadersIdx::HDMI_LOOPBACK];
    }

    const AudioBuffer& AudioReaders::micBuff() const {
        return micAudioReader().buffer();
    }

    const AudioBuffer& AudioReaders::spkBuff() const {
        return spkAudioReader().buffer();
    }

    const AudioBuffer& AudioReaders::lbBuff() const {
        return lbAudioReader().buffer();
    }

    const AudioBuffer& AudioReaders::hdmiLbBuff() const {
        return hdmiAudioReader().buffer();
    }

} // namespace quasar
