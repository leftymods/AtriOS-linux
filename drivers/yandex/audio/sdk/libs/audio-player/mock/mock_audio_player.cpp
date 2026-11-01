#include "mock_audio_player.h"

#include <memory>

using namespace quasar;

MockAudioPlayer::MockAudioPlayer(const AudioPlayer::Params& params)
    : AudioPlayer(params)
{
    onProgressThread_ = std::thread(&MockAudioPlayer::sendProgressLoop, this);
}

MockAudioPlayer::~MockAudioPlayer() {
    sendBufferingEnd();
    stopped_ = true;
    notified = true;
    isPlaying_ = false;
    condVar_.notify_all();

    if (onProgressThread_.joinable()) {
        onProgressThread_.join();
    }
}

void MockAudioPlayer::sendProgressLoop() {
    while (!stopped_) {
        if (isPlaying_.load()) {
            sendProgress();
        }
        std::unique_lock<std::mutex> lock(mutex_);
        condVar_.wait_for(lock, std::chrono::milliseconds(100), [&]() {
            return stopped_.load() || notified.load();
        });
        notified = false;
    }
}

const std::vector<AudioPlayer::Format>& MockAudioPlayer::supportedFormats() const {
    static std::vector<AudioPlayer::Format> formats{AudioPlayer::Format::FMT_WAV, AudioPlayer::Format::FMT_RAW, AudioPlayer::Format::FMT_MP3};
    return formats;
}

bool MockAudioPlayer::replayAsync() {
    std::lock_guard<std::mutex> guard(mutex_);
    isPlaying_ = true;
    notified = true;
    condVar_.notify_all();
    return true;
}

bool MockAudioPlayer::playAsync() {
    std::lock_guard<std::mutex> guard(mutex_);
    isPlaying_ = true;
    notified = true;
    condVar_.notify_all();
    sendStart();
    return true;
}

bool MockAudioPlayer::pause() {
    std::lock_guard<std::mutex> guard(mutex_);
    isPlaying_ = false;
    notified = true;
    condVar_.notify_all();
    return true;
}

bool MockAudioPlayer::stop() {
    std::lock_guard<std::mutex> guard(mutex_);
    isPlaying_ = false;
    notified = true;
    condVar_.notify_all();
    return true;
}

bool MockAudioPlayer::seek(int /* seconds */) {
    return false;
}

bool MockAudioPlayer::isPlaying() const {
    return isPlaying_.load();
}

bool MockAudioPlayer::startBuffering() {
    return false;
}

bool MockAudioPlayer::setVolume(double /* volume */) {
    return false;
}
int MockAudioPlayer::position() const {
    return -1;
}
int MockAudioPlayer::duration() const {
    return -1;
}

/* MockPlayerFactory */

MockPlayerFactory::MockPlayerFactory()
    : params_(createParams())
{
}

std::unique_ptr<AudioPlayer> MockPlayerFactory::createPlayer(const AudioPlayer::Params& /* params */) {
    return std::unique_ptr<AudioPlayer>(new MockAudioPlayer(*params_));
};

std::unique_ptr<AudioPlayer::Params> MockPlayerFactory::createParams() {
    return std::make_unique<AudioPlayer::Params>();
}
