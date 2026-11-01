#include "sound_player.h"

#include <yandex_io/libs/logging/logging.h>

#include <iostream>

YIO_DEFINE_LOG_MODULE("audio_sender");

using namespace AudioSender;

SoundPlayer::SoundPlayer(SpeechKit::AudioPlayer::SharedPtr audioPlayer)
    : audioPlayer(std::move(audioPlayer))
{
}

void SoundPlayer::start(std::list<SpeechKit::SoundBuffer::SharedPtr> buffers, std::function<void(bool)> endCallback) {
    if (buffers.empty() || buffers.front()->getInfo().getChannelCount() != 1) {
        endCallback(false);
        return;
    }
    this->endCallback = std::move(endCallback);

    std::vector<uint8_t> concatData;
    for (const auto& buffer : buffers) {
        concatData.insert(concatData.end(), buffer->getData().begin(), buffer->getData().end());
    }
    auto soundBuffer = std::make_shared<SpeechKit::SoundBuffer>(buffers.front()->getInfo(), concatData);
    audioPlayer->subscribe(shared_from_this());
    audioPlayer->playData(soundBuffer);
    audioPlayer->setDataEnd();
}

void SoundPlayer::stop() {
    audioPlayer->cancel();
}

void SoundPlayer::onPlayingDone(SpeechKit::AudioPlayer::SharedPtr /*player*/) {
    YIO_LOG_DEBUG("SoundPlayer::onPlayingDone");
    endCallback(true);
    endCallback = std::function<void(bool)>();
    audioPlayer->unsubscribe(shared_from_this());
}

void SoundPlayer::onPlayingError(SpeechKit::AudioPlayer::SharedPtr /*player*/, const SpeechKit::Error& err) {
    YIO_LOG_ERROR_EVENT("SoundPlayer.onPlayingError", err.getString());
    endCallback(false);
    endCallback = std::function<void(bool)>();
    audioPlayer->unsubscribe(shared_from_this());
}
