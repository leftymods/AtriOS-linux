#pragma once

#include <yandex_io/libs/audio/reader/audio_reader_config.h>
#include <yandex_io/libs/audio/tinyalsa/tiny_alsa_audio_reader.h>
#include <yandex_io/modules/audio_input/audio_device/stats/audio_device_stats.h>
#include <yandex_io/modules/audio_input/channels_splitter/channels_splitter.h>
#include <yandex_io/sdk/audio_source/feedback_source.h>

#include <voicetech/vqe/libs/downsampling/downsampling.h>
#include <voicetech/vqe/libs/downsampling/filter_designs.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

using AudioBuffer = std::vector<uint8_t>;
using Downsampler = vqe::BufferedDownsampler<vqe::MultiChannelDownsampler<float, float>>;

namespace quasar {

    class IYandexStation2AudioReader {
    public:
        using Ptr = std::unique_ptr<IYandexStation2AudioReader>;

        IYandexStation2AudioReader(int channelsUsed, AudioReaderConfig config);
        IYandexStation2AudioReader(int channelsUsed, AudioReaderConfig config, size_t bufferSize);

        virtual ~IYandexStation2AudioReader();

        int channelsUsed() const;
        const AudioReaderConfig& config() const;
        const AudioBuffer& buffer() const;

        virtual void readDataOrThrow(std::shared_ptr<YandexIO::AudioDeviceStats::ChunkSession> logSession, YandexIO::AudioDeviceStats::Tag statsTag) = 0;
        virtual void read() = 0;
        virtual const Downsampler::OutputVectorType& downsample(const Downsampler::InputVectorType& in) = 0;

        virtual void close() = 0;
        virtual void start() = 0;
        virtual void stop() = 0;
        virtual void tryRecover() = 0;
        virtual bool wait(std::chrono::milliseconds timeout) = 0;
        virtual unsigned int getBufferSize() = 0;

    protected:
        int channelsUsed_;
        const AudioReaderConfig config_;
        AudioBuffer buffer_;
    };

    class ZerosAudioReader: public IYandexStation2AudioReader {
    public:
        ZerosAudioReader(int channelsUsed, size_t bufferSize);

        void readDataOrThrow(std::shared_ptr<YandexIO::AudioDeviceStats::ChunkSession> logSession, YandexIO::AudioDeviceStats::Tag statsTag) override;
        void read() override;
        const Downsampler::OutputVectorType& downsample(const Downsampler::InputVectorType& in) override;

        void close() override;
        void start() override;
        void stop() override;
        void tryRecover() override;
        bool wait(std::chrono::milliseconds /* wait */) override;
        unsigned int getBufferSize() override;

    private:
        Downsampler::OutputVectorType downsampled_;
    };

    class YandexStation2AudioReader: public IYandexStation2AudioReader {
    public:
        using ChannelsGetter = std::function<int(const AudioReaderConfig&)>;

        YandexStation2AudioReader(const YandexStation2AudioReader&) = delete;
        YandexStation2AudioReader(YandexStation2AudioReader&&) = delete;

        YandexStation2AudioReader& operator=(const YandexStation2AudioReader&) = delete;
        YandexStation2AudioReader& operator=(YandexStation2AudioReader&&) = delete;

        YandexStation2AudioReader(AudioReaderConfig config, std::unique_ptr<TinyAlsaAudioReader> reader, int channelsUsed, Downsampler downsampler, std::string_view logTag);

        static Ptr createSpk(const Json::Value& config, const std::string& deviceType, const vqe::DigitalFilterConfig& vqeConfig, std::string_view logTag);
        static Ptr createMic(const Json::Value& config, const std::string& deviceType, const vqe::DigitalFilterConfig& vqeConfig, std::string_view logTag, bool useHwSync);

        ~YandexStation2AudioReader();

        void readDataOrThrow(std::shared_ptr<YandexIO::AudioDeviceStats::ChunkSession> logSession, YandexIO::AudioDeviceStats::Tag statsTag) override;
        void read() override;
        const Downsampler::OutputVectorType& downsample(const Downsampler::InputVectorType& in) override;

        void close() override;
        void start() override;
        void stop() override;
        void tryRecover() override;
        bool wait(std::chrono::milliseconds timeout) override;
        unsigned int getBufferSize() override;

    private:
        std::unique_ptr<TinyAlsaAudioReader> reader_;
        Downsampler downsampler_;
        const std::string logTag_;
    };

    class AudioReaders {
    public:
        using Readers = std::array<IYandexStation2AudioReader::Ptr, 4>;

        AudioReaders(Readers readers, bool useHwFeedbackSync, YandexIO::FeedbackSource hardwareSyncTarget);
        ~AudioReaders();

        AudioReaders(const AudioReaders&) = delete;
        AudioReaders(AudioReaders&& audioReaders) noexcept;

        AudioReaders& operator=(const AudioReaders&) = delete;
        AudioReaders& operator=(AudioReaders&&) = delete;

        void stop();
        void start();
        void waitDevicesReady();
        void close();
        void tryRecover();

        bool read(AudioBuffer& micBuff, AudioBuffer& spkBuff, AudioBuffer& lbBuff, AudioBuffer& hdmiLbBuff,
                  std::shared_ptr<YandexIO::AudioDeviceStats::ChunkSession> logSession);

        const std::vector<float>& downsampleSpk(const AudioBuffer& data);
        const std::vector<float>& downsampleMic(const AudioBuffer& data);
        const std::vector<float>& downsampleLb(const AudioBuffer& data);
        const std::vector<float>& downsampleHdmi(const AudioBuffer& data);

        int micChannelsUsed() const;
        int spkChannelsUsed() const;
        int lbChannelsUsed() const;
        int hdmiChannelsUsed() const;

        static constexpr float SAMPLE_FLOAT_SCALE = 32768.0f;

    private:
        void checkAudioReaderConfigs() const;

        bool readWithoutHwSync(AudioBuffer& micBuff, AudioBuffer& spkBuff, AudioBuffer& lbBuff, AudioBuffer& hdmiLbBuff,
                               std::shared_ptr<YandexIO::AudioDeviceStats::ChunkSession> logSession);
        bool readWithHwSync(AudioBuffer& micBuff, AudioBuffer& spkBuff, AudioBuffer& lbBuff, AudioBuffer& hdmiLbBuff,
                            std::shared_ptr<YandexIO::AudioDeviceStats::ChunkSession> logSession);

        const IYandexStation2AudioReader& spkAudioReader() const;
        const IYandexStation2AudioReader& micAudioReader() const;
        const IYandexStation2AudioReader& lbAudioReader() const;
        const IYandexStation2AudioReader& hdmiAudioReader() const;

        IYandexStation2AudioReader& spkAudioReader();
        IYandexStation2AudioReader& micAudioReader();
        IYandexStation2AudioReader& lbAudioReader();
        IYandexStation2AudioReader& hdmiAudioReader();

        const AudioBuffer& micBuff() const;
        const AudioBuffer& spkBuff() const;
        const AudioBuffer& lbBuff() const;
        const AudioBuffer& hdmiLbBuff() const;

        const bool useHwFeedbackToMicSync_;
        Readers readers_;
        std::unique_ptr<YandexIO::IChannelsSplitter> splitter_;
        YandexIO::FeedbackSource hardwareSyncTarget_;
    };

} // namespace quasar
