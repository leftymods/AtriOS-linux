#include "audio_dumper.h"

#include <yandex_io/libs/logging/logging.h>

#include <atomic>
#include <signal.h>

namespace {

    std::atomic_bool gShouldDump{false};

    void handleSignal(int signal) {
        if (signal == SIGUSR1) {
            YandexIO::AudioDumper::startDump();
        } else if (SIGUSR2) {
            YandexIO::AudioDumper::stopDump();
        }
    }

} // namespace

namespace YandexIO {

    void AudioDumper::initSignalHandlers() {
        struct sigaction sig_action;

        sig_action.sa_handler = &handleSignal;
        sig_action.sa_flags = 0;
        sigemptyset(&sig_action.sa_mask);

        sigaction(SIGUSR1, &sig_action, nullptr);
        sigaction(SIGUSR2, &sig_action, nullptr);
    }

    bool AudioDumper::shouldDump() {
        return gShouldDump.load();
    }

    void AudioDumper::startDump() {
        gShouldDump.store(true);
    }

    void AudioDumper::stopDump() {
        gShouldDump.store(false);
    }

    AudioDumper::AudioDumper(std::string dumpDirPath)
        : dumpDirPath_(std::move(dumpDirPath))
    {
    }

    AudioDumper::~AudioDumper() {
        closeDumpFiles();
    }

    void AudioDumper::onAudioData(const ChannelsData& data) {
        if (!shouldDump()) {
            /* close files if have opened */
            closeDumpFiles();
            return;
        }
        for (const auto& channel : data) {
            if (channel.data.empty()) {
                /* skip if channel for some reason is empty */
                continue;
            }
            const auto filename = dumpDirPath_ + channel.name + ".raw";
            const auto it = nameToFile_.find(filename);
            if (it == nameToFile_.end() || it->second == nullptr) {
                auto file = fopen(filename.c_str(), "wb");
                if (file == nullptr) {
                    YIO_LOG_WARN("audio dumping error: fopen fails. errno: " << errno);
                    continue;
                }
                nameToFile_[filename] = file;
            }
            fwrite(channel.data.data(), sizeof(channel.data[0]), channel.data.size(), nameToFile_[filename]);
            fflush(nameToFile_[filename]);
        }
    }

    void AudioDumper::closeDumpFiles() {
        for (auto& nameToFile : nameToFile_) {
            if (nameToFile.second != nullptr) {
                fclose(nameToFile.second);
                nameToFile.second = nullptr;
            }
        }
    }

} /* namespace YandexIO */
