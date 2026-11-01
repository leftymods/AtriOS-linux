#include "devnull_audio_player.h"

#include <yandex_io/libs/logging/logging.h>

#include <algorithm>

using namespace quasar;

DevnullAudioPlayer::DevnullAudioPlayer()
    : AudioPlayer()
    , playingQueue_(128)
{
    YIO_LOG_TRACE("created");
    playingThread_ = std::thread(&DevnullAudioPlayer::playingThread, this);
}

DevnullAudioPlayer::~DevnullAudioPlayer()
{
    threadRunning_ = false;
    playingQueue_.clear();
    /* Push, so thread will wake up from BlockingQueue::pop method */
    playingQueue_.push(nullptr);
    playingThread_.join();
}

std::shared_ptr<DevnullAudioPlayer> DevnullAudioPlayer::create()
{
    return std::make_shared<DevnullAudioPlayer>();
}

void DevnullAudioPlayer::play()
{
    YIO_LOG_TRACE("play");

    isPlaying_ = true;
    for (AudioPlayerListener::WeakPtr listener : listeners_) {
        auto listenerPtr = listener.lock();
        if (listenerPtr != nullptr) {
            listenerPtr->onPlayingBegin(shared_from_this());
        }
    }
}

/**
 * @~russian
 * @brief Приостанавливает текущее воспроизведение.
 */
/**
 * @~english
 */
void DevnullAudioPlayer::pause()
{
    YIO_LOG_TRACE("pause");

    isPlaying_ = false;
    for (AudioPlayerListener::WeakPtr listener : listeners_) {
        auto listenerPtr = listener.lock();
        if (listenerPtr != nullptr) {
            listenerPtr->onPlayingPaused(shared_from_this());
        }
    }
}

/**
 * @~russian
 * @brief Отменяет текущее воспроизведение.
 */
/**
 * @~english
 */
void DevnullAudioPlayer::cancel()
{
    /* Clear queue because speech ended */
    playingQueue_.clear();
    cancelStream_ = true;
    isPlaying_ = false;
    YIO_LOG_TRACE("cancel");
}

/**
 * @~russian
 * @brief Передача аудио данных для воспроизведения.
 *
 * После передачи всех данных необходимо вызвать функцию-член {@link .setDataEnd()}.
 *
 * @param buffer данные для воспроизведения
 * @see SoundBuffer
 */
/**
 * @~english
 */
void DevnullAudioPlayer::playData(SpeechKit::SoundBuffer::SharedPtr buffer)
{
    const std::vector<uint8_t>& data = buffer->getData();

    YIO_LOG_TRACE("playData, data.size=" << data.size());

    const auto& info = buffer->getInfo();
    YIO_LOG_TRACE("Format: " << info.getFormat() << " Channels: " << info.getChannelCount()
                             << " SampleRate: " << info.getSampleRate() << " Size: " << data.size());

    /* Add buffer to play in playing Thread */
    playingQueue_.push(std::move(buffer));

    if (!isPlaying_) {
        play();
    }
}

/**
 * @~russian
 * @brief Устанавливает флаг окончания передачи аудио данных.
 */
/**
 * @~english
 */
void DevnullAudioPlayer::setDataEnd()
{
    YIO_LOG_TRACE("setDataEnd");

    waitingPlayingDone_ = true;
    for (AudioPlayerListener::WeakPtr listener : listeners_) {
        if (auto slistener = listener.lock()) {
            slistener->onPlayingDone(shared_from_this());
        }
    }
}

/**
 * @~russian
 * @brief Задаёт множитель громкости для исходных сэмплов.
 */
/**
 * @~english
 */
void DevnullAudioPlayer::setVolume(float gain)
{
    YIO_LOG_TRACE("setVolume " << gain);
}

/**
 * @~russian
 * @brief Возвращает текущий множитель громкости для исходных сэмплов.
 */
/**
 * @~english
 */
float DevnullAudioPlayer::getVolume() const {
    YIO_LOG_TRACE("getVolume");
    return 0;
}

/**
 * @~russian
 * @brief Добавляет слушателя.
 */
/**
 * @~english
 */
void DevnullAudioPlayer::subscribe(AudioPlayerListener::WeakPtr listener)
{
    YIO_LOG_TRACE("subscribe");
    if (listeners_.end() == std::find_if(listeners_.begin(), listeners_.end(),
                                         [listener](AudioPlayer::AudioPlayerListener::WeakPtr wp) {
                                             return !(wp.owner_before(listener) || listener.owner_before(wp));
                                         }))
    {
        listeners_.push_back(listener);
    }
}

/**
 * @~russian
 * @brief Удаляет слушателя.
 */
/**
 * @~english
 */
void DevnullAudioPlayer::unsubscribe(AudioPlayerListener::WeakPtr listener)
{
    YIO_LOG_TRACE("unsubscribe");

    listeners_.remove_if([listener](AudioPlayer::AudioPlayerListener::WeakPtr wp) {
        return !(wp.owner_before(listener) || listener.owner_before(wp));
    });
}

void DevnullAudioPlayer::playingThread()
{
    while (threadRunning_) {
        ::SpeechKit::SoundBuffer::SharedPtr buffer;
        playingQueue_.pop(buffer);
        /* Destructor pass nullptr buffer. Check threadRunning_ on next iteration */
        if (buffer == nullptr) {
            continue;
        }
        /* If stream was canceled -> queue will be empty. Reset flag for next data chunks */
        cancelStream_ = false;
    } /* while(threadRunning_) */
}
