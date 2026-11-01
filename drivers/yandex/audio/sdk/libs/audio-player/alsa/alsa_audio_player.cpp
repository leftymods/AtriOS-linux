#include "alsa_audio_player.h"

#include "wav_decoder.h"

#include <yandex_io/libs/audio/alsa/alsa_audio_writer.h>
#include <yandex_io/libs/logging/logging.h>

#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace quasar;

AlsaAudioPlayer::AlsaAudioPlayer(const AudioPlayer::Params& params)
    : AudioPlayer(params)
    , curTrack_(this)
{
}

const std::vector<AudioPlayer::Format>& AlsaAudioPlayer::supportedFormats() const {
    static std::vector<AudioPlayer::Format> formats{AudioPlayer::Format::FMT_WAV};
    return formats;
}

bool AlsaAudioPlayer::playAsync() {
    if (curTrack_.isPlaying()) {
        YIO_LOG_WARN("AlsaAudioPlayer is playing track. Discarding request");
        return false;
    }

    curTrack_.lock();

    try {
        curTrack_.open(params_.alsaDevice(), params_.filePath());
        curTrack_.startUnlock();
    } catch (std::runtime_error& e) {
        sendError(e.what());
        curTrack_.unlock();
        return false;
    }

    return true;
}

bool AlsaAudioPlayer::pause() {
    if (!curTrack_.isPlaying()) {
        YIO_LOG_WARN("Stopping track while it is not running");
        return false;
    }

    curTrack_.stop();

    return true;
}

AlsaAudioPlayer::Track::Track(AlsaAudioPlayer* player)
    : player_(player)
{
    playbackThread_ = std::thread(&AlsaAudioPlayer::Track::playbackThread, this);
}

AlsaAudioPlayer::Track::~Track() {
    playbackThreadStopped_ = true;
    playbackThreadCondVar_.notify_one();

    playbackThread_.join();

    close();
}

void AlsaAudioPlayer::Track::open(const std::string& deviceName, const std::string& path) {
    if (isOpen_) {
        YIO_LOG_WARN("Opening already opened track!");
        close();
    }

    file_ = fopen(path.c_str(), "rb");
    if (!file_) {
        throw std::runtime_error("Error opening audio file");
    }

    const auto& wavHeader = WavDecoder::decode(file_);

    audioWriter_ = std::make_unique<AlsaAudioWriter>();
    audioWriter_->open(deviceName, wavHeader.channelsNum, wavHeader.rate, wavHeader.fmt);

    audioBuf_.resize(audioWriter_->bufSize());

    YIO_LOG_INFO("Open ALSA device: " << deviceName << ", rate=" << wavHeader.rate
                                      << ", fmt=" << wavHeader.fmt << ", buffer size=Audio buffer size="
                                      << audioWriter_->bufSize());

    isOpen_ = true;
}

void AlsaAudioPlayer::Track::close() {
    if (!isOpen_) {
        YIO_LOG_WARN("Closing not opened track!");
    }

    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }

    isOpen_ = false;
}

void AlsaAudioPlayer::Track::startUnlock() {
    YIO_LOG_INFO("Starting track");

    stopped_.store(false);

    playbackState_ = PlaybackState::PLAY_SCHEDULED;

    playbackThreadLock_.unlock();
    playbackThreadCondVar_.notify_one();
}

void AlsaAudioPlayer::Track::stop() {
    YIO_LOG_INFO("Stopping track");

    stopped_.store(true);
}

void AlsaAudioPlayer::Track::playbackThread() {
    size_t bytesRead{};

    bool failed = false;
    bool interrupted = false;
    std::string error;
    std::unique_lock lock(playbackThreadLock_, std::defer_lock);

    while (!playbackThreadStopped_) {
        switch (playbackState_) {
            case PlaybackState::PLAY_SCHEDULED:
            case PlaybackState::PLAY_ACTIVE: {
                lock.lock();
                playbackThreadCondVar_.wait(lock,
                                            [this] { return playbackThreadStopped_ || this->playbackState_ == PlaybackState::PLAY_SCHEDULED; });

                if (playbackThreadStopped_) {
                    lock.unlock();
                    break;
                }

                do {
                    bytesRead = fread(audioBuf_.data(), 1, audioWriter_->bufSize(), file_);

                    if (!audioWriter_->write(audioBuf_.data(), bytesRead)) {
                        failed = true;
                        error = audioWriter_->getError();
                        break;
                    }
                } while (bytesRead > 0 && isPlaying());

                if (!isPlaying()) {
                    interrupted = true;
                }

                playbackState_ = PlaybackState::PLAY_FINISHED;
            }

            case PlaybackState::PLAY_FINISHED: {
                playbackState_ = PlaybackState::PLAY_ACTIVE;

                stop();
                close();

                lock.unlock();

                if (interrupted) {
                    player_->sendPaused();
                } else if (!failed) {
                    player_->sendEnd();
                } else {
                    player_->sendError(error);
                }

                break;
            }
        }
    }
}
