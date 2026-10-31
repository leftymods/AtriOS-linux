#pragma once

#include <yandex_io/sdk/audio_source/i_audio_source_client.h>

#include <cstdio>
#include <map>
#include <string>

namespace YandexIO {

    class AudioDumper: public IAudioSourceClient::Listener {
    public:
        explicit AudioDumper(std::string dumpDirPath);
        ~AudioDumper();

        /*
         * Setup USR1 and USR2 signal handlers.
         * When received USR1 - all AudioDumpers will dump data from
         * onAudioData to files (dumpDirPath + channelName + ".raw")
         */
        static void initSignalHandlers();

        static void startDump();
        static void stopDump();

    public:
        /* AudioSourceClient interface */
        void onAudioData(const ChannelsData& data) override;

    private:
        static bool shouldDump();

        void closeDumpFiles();

        const std::string dumpDirPath_;
        std::map<std::string, FILE*> nameToFile_;
    };

} // namespace YandexIO
