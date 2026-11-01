#pragma once

#include <yandex_io/libs/threading/blocking_queue.h>

#include <speechkit/AudioPlayer.h>
#include <speechkit/SoundBuffer.h>

#include <atomic>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace quasar {
    class DevnullAudioPlayer: public SpeechKit::AudioPlayer,
                              public std::enable_shared_from_this<DevnullAudioPlayer> {
    public:
        DevnullAudioPlayer();
        ~DevnullAudioPlayer() override;
        static std::shared_ptr<DevnullAudioPlayer> create();

        /**
         * @~russian
         * @brief Начинает или возобновляет воспроизведение.
         */
        /**
         * @~english
         */
        void play() override;

        /**
         * @~russian
         * @brief Приостанавливает текущее воспроизведение.
         */
        /**
         * @~english
         */
        void pause() override;

        /**
         * @~russian
         * @brief Отменяет текущее воспроизведение.
         */
        /**
         * @~english
         */
        void cancel() override;

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
        void playData(SpeechKit::SoundBuffer::SharedPtr buffer) override;

        /**
         * @~russian
         * @brief Устанавливает флаг окончания передачи аудио данных.
         */
        /**
         * @~english
         */
        void setDataEnd() override;

        /**
         * @~russian
         * @brief Задаёт множитель громкости для исходных сэмплов.
         */
        /**
         * @~english
         */
        void setVolume(float gain) override;

        /**
         * @~russian
         * @brief Возвращает текущий множитель громкости для исходных сэмплов.
         */
        /**
         * @~english
         */
        float getVolume() const override;

        /**
         * @~russian
         * @brief Добавляет слушателя.
         */
        /**
         * @~english
         */
        void subscribe(AudioPlayerListener::WeakPtr listener) override;

        /**
         * @~russian
         * @brief Удаляет слушателя.
         */
        /**
         * @~english
         */
        void unsubscribe(AudioPlayerListener::WeakPtr listener) override;

    private:
        void playingThread();

        std::atomic_bool waitingPlayingDone_{false};

        std::list<AudioPlayerListener::WeakPtr> listeners_;
        quasar::BlockingQueue<SpeechKit::SoundBuffer::SharedPtr> playingQueue_;
        std::atomic_bool threadRunning_{true};
        std::thread playingThread_;
        std::string alsaDevice_;
        std::atomic_bool cancelStream_{false};
        std::atomic_bool isPlaying_{false};
    };
} // namespace quasar
