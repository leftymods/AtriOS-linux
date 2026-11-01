#pragma once

#include <yandex_io/services/mediad/media/players/player.h>

#include <yandex_io/libs/base/retry_delay_counter.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/websocket/websocket_client.h>

#include <json/json.h>

#include <atomic>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace quasar {
    /**
     * Здесь реализовано все взаимодействие с Websocket-API Яндекс.Музыки.
     * Описание запросов и ответов API - https://wiki.yandex-team.ru/users/psmcd/Quasar-pleer-API
     *
     * Общаемся с музыкой по протоколу WebSocket, адрес указан в конфиге в mediad.apiUrl. Сообщения отсылаются в методе
     * request, получаются - в методе onMessage. Каждому запросу присваивается уникальный идентификатор reqId. Такой же
     * идентификатор будет записан в поле reqId ответа, чтобы можно было их сопоставить.
     *
     * Сопоставление происходит так: перед отправкой запроса, в словарь requestPromises_ записывается новый промис по
     * ключу, равному reqId запроса. Сразу после этого request() начинает ждать выполнения этого промиса. Вместе с этим,
     * метод onMessage при получении сообщения message берет промис из requestPromises_ по ключу message.reqId и
     * устанавливает ему значение, равное msg. Таким образом, метод request получает и возвращает ответ на свой запрос.
     *
     * Основное взаимодействие с бэкендом музыки осуществляется вызовами sync, feedback и requestUrl:
     * - sync: получает обновления по текущей очереди воспроизведения в виде команд "добавить треки" и "удалить треки".
     *      При добавлении треков тело ответа sync сразу содержит всю нужную метаинформацию о треке, кроме ссылки на его
     *      файл.
     * - feedback: отправляет на бэкенд действия пользователя/плеера над текущим треком, например лайк/дизлайк,
     *      старт/пауза.
     * - requestUrl: получить url файла трека для воспроизведения.
     *
     * Помимо этого есть еще методы auth и ping:
     * - auth: авторизует соединение, должен вызываться единожды, предшествуя любым другим вызовам, кроме ping
     * - ping: heartbeat-сообщение для бэкенда, отправляемое каждую секунду, чтобы Музыка не разорвала с нами соединение.
     */
    class YandexMusic {
    public:
        const static int RETRIES_COUNT;
        const static int RETRIES_COUNT_BEFORE_RECONNECT;
        const static int WS_REQUEST_TIMEOUT_MS;
        const static int TOTAL_REQUEST_TIMEOUT_MS;
        const static int PING_INTERVAL_SEC;
        const static bool SHOULD_RETRY_PING;
        const static bool GENERATE_RETRY_ID;
        const static int CONNECT_TIMEOUT_MS;
        const static int PROMISES_LEAK_LIMIT;

        enum class STATUS {
            LIKED = 1,
            DISLIKED = 2,
            UNKNOWN = 0
        };

        struct Track {
            std::string id;
            std::string batchId;
            Json::Value batchInfo;
            std::string url;
            std::atomic<STATUS> status;
            Json::Value fullJsonInfo;
            std::string artists;
            std::string title;
            std::string coverUri;
            std::string type;
            std::string albumGenre;

            double durationMs;

            std::string shotUrl;   // shot audio url if shot associated (playable)
            std::string shotId;    // id of shot if present
            double shotDurationMs; // duration of shot if present or zero
            bool isShot;           // true if this track is treated as shot, not handling in queue

            using Normalization = AudioPlayer::Params::Normalization;
            std::optional<Normalization> normalization;
        };

        struct PlaylistInfo {
            std::string id;
            std::string type;
            std::string description;
            bool shuffled;
            std::string repeatMode;
        };

        struct Params {
            enum class Ssl {
                Yes,
                No,
            };
            enum class AutoPing {
                Yes,
                No,
            };

            Ssl ssl;
            AutoPing autoPing;
        };

        using OnAuthFailedHandler = std::function<void()>;

        YandexMusic(std::shared_ptr<YandexIO::IDevice> device, const Params& params,
                    const Json::Value& customYandexMusicConfig, OnAuthFailedHandler onAuthFailedHandler);
        ~YandexMusic();

        /**
         * Отправляет фидбек остановки музыки если до этого был вызван start
         */
        void stop();

        /**
         * Получить и вернуть первый трек. На бэкенд Музыки отправляется сообщение, что этот трек начал проигрываться.
         * Метод нужно вызывать, когда музыка еще не начала играть.
         * @return Текущий трек
         * @throw RequestTimeoutException
         */
        std::shared_ptr<YandexMusic::Track> start();

        /**
         * Получить и вернуть следующий трек. На бэкенд Музыки отправляется сообщение, что этот трек начал проигрываться.
         * Этот метод нужно вызывать, когда музыка уже играет.
         * @param skip true, если пользователь сказал "Дальше", не дослушав трек до конца.
         * @return Следующий трек
         * @throw RequestTimeoutException
         */
        std::shared_ptr<YandexMusic::Track> next(bool skip = true);

        /**
         * Получить и вернуть предыдущий трек. На бэкенд Музыки отправляется сообщение, что этот трек начал
         * проигрываться.
         * @return Предыдущий трек
         * @throw RequestTimeoutException
         */
        std::shared_ptr<YandexMusic::Track> prev();

        /**
         * Перезапускает воспроизведение, если проигрыватель уже запущен. На бекенд музыки отправляется сообщение,
         * что начал проигрываться новый трек.
         * @return Следующий трек
         * @throw RequestTimeoutException
         */
        std::shared_ptr<YandexMusic::Track> restart();

        /**
         * Поставить лайк текущему треку.
         * @throw RequestTimeoutException
         */
        void like();

        /**
         * Поставить дизлайк текущему треку.
         * @throw RequestTimeoutException
         */
        void dislike();

        /**
         * Получить информацию о текущей очереди воспроизведения.
         * @return Информация о текущем плейлисте.
         */
        YandexMusic::PlaylistInfo getCurrentPlaylist() const;

        /**
         * Получить текущий трек из локальной очереди. Если у трека еще неизвестен url, то он будет запрошен с сервера.
         * @return Текущий трек с заполненным url, или nullptr если очередь треков пуста.
         */
        std::shared_ptr<YandexMusic::Track> getCurrentTrack() const;

        /**
         * Получить следующий трек из локальной очереди. В отличие от getCurrentTrack() поле url не заполняется.
         * @return Следующий трек, или nullptr, если в локальной очереди больше нет треков.
         */
        std::shared_ptr<YandexMusic::Track> getNextTrack() const;

        void setAuthData(const std::string& uid, const std::string& sessionId, const std::string& token, const std::string& deviceId);
        void setCurrentProgress(int position, int duration);
        void setCurrentPosition(int position);
        unsigned int getCurrentPosition() const;
        unsigned int getCurrentDuration() const;
        Player::ChangeConfigResult updateConfig(const Json::Value& customYandexMusicConfig);
        Player::ChangeConfigResult updateWebsocketSettings(const Json::Value& customYandexMusicConfig) const;
        Player::ChangeConfigResult updateWebsocketLogChannels(const Json::Value& customYandexMusicConfig);
        Player::ChangeConfigResult updatePingSettings(const Json::Value& customYandexMusicConfig);
        Player::ChangeConfigResult updateConnectionSettings(const Json::Value& customYandexMusicConfig);
        Player::ChangeConfigResult updateBitRateSettings(const Json::Value& customYandexMusicConfig);

        void ignoreNextEnd();

    private:
        /**
         * Сообщает бэкенду, что текущий трек либо завершен, либо пропущен.
         * Обновляет плейлист, если больше треков нет.
         * @param skip true, если пользователь сказал "Дальше", не дослушав трек до конца.
         * @param startRequestTs time_point, относительно которого следим за таймаутом totalRequestTimeoutMs_
         * @throw RequestTimeoutException
         */
        void rewindForward(bool skip, const std::chrono::steady_clock::time_point& startRequestTs);

        /**
         * Сообщает бэкенду, что текущий трек пропущен при перемотке на предыдущий трек.
         * Обновляет плейлист, если больше треков нет.
         * @param startRequestTs time_point, относительно которого следим за таймаутом totalRequestTimeoutMs_
         * @throw RequestTimeoutException
         */
        void rewindBackward(const std::chrono::steady_clock::time_point& startRequestTs);

        /**
         * Сообщает бэкенду, что текущий трек начат.
         * Обновляет плейлист и запрашивает url трека
         * @param startRequestTs time_point, относительно которого следим за таймаутом totalRequestTimeoutMs_
         * @return Текущий трек
         * @throw RequestTimeoutException
         */
        std::shared_ptr<YandexMusic::Track> startCurrentTrack(const std::chrono::steady_clock::time_point& startRequestTs);

        /**
         * Синхронизирует локальную очередь воспроизведения с той, которая сформирована для текущей сессии
         * на бэкенде Музыки.
         * Чтобы поддерживать локальную очередь в актуальном состоянии, следует вызывать sync после feedback и перед
         * чтением треков из локальной очереди.
         * @param startRequestTs time_point, относительно которого следим за таймаутом totalRequestTimeoutMs_
         * @throw RequestTimeoutException
         */
        void sync(const std::chrono::steady_clock::time_point& startRequestTs);

        /**
         * Сообщает бэкенду о каком-то действии пользователя или плеера над очередью. Это может быть лайк/дизлайк,
         * начало/конец воспроизведения, запрос предыдущей/следующей песни. Любое из этих действий может потенциально
         * изменить последующую очередь воспроизведения, поэтому после вызовов feedback нужно вызывать sync, чтобы
         * своевременно получить эти изменения.
         * @param track Трек, над которым выполняется действие.
         * @param type Название действия, выполняемое над треком.
         * @param startRequestTs time_point, относительно которого следим за таймаутом totalRequestTimeoutMs_
         * @throw RequestTimeoutException
         */
        void feedback(const std::shared_ptr<YandexMusic::Track>& track, const std::string& type,
                      const std::chrono::steady_clock::time_point& startRequestTs);

        /**
         * Запросить url для файла трека. Url возвращаются каждый раз разные и живут 1 час.
         * Информация о треках приходит к нам в результате вызовов sync и изначально не содержит url. Так сделано,
         * потому что ссылки на треки со временем "протухают", а значит получать их надо лишь непосредственно перед
         * воспроизведением. Вместе с url приходят параметры нормализации (r128)
         * @param track Трек, для которого хотим получить ссылку.
         * @param startRequestTs time_point, относительно которого следим за таймаутом totalRequestTimeoutMs_
         * @return Прямая ссылка на файл трека.
         * @throw RequestTimeoutException
         */
        void requestUrlForTrack(YandexMusic::Track& track, const std::chrono::steady_clock::time_point& startRequestTs);

        /**
         * Запросить url для трека и записать его в переданный track. Если переданный трек - шот, то не делать ничего.
         * @param track Трек, в которой хотим записать ссылку.
         * @param startRequestTs time_point, относительно которого следим за таймаутом totalRequestTimeoutMs_
         * @throw RequestTimeoutException
         */
        void setUrlForTrack(const std::shared_ptr<YandexMusic::Track>& track,
                            const std::chrono::steady_clock::time_point& startRequestTs);

        /**
         * Авторизует текущее websocket-соединение на сервере. Метод должен вызываться 1 раз за соединение, прежде чем
         * будут вызваны любые из запросов, требующие авторизацию.
         *
         * Должен вызываться ТОЛЬКО под локом connectionMutex_.
         *
         * @param startRequestTs time_point, относительно которого следим за таймаутом totalRequestTimeoutMs_
         * @return true, если авторизация прошла успешно
         * @throw AuthorizationFailedException
         * @throw RequestTimeoutException
         */
        bool auth(const std::chrono::steady_clock::time_point& startRequestTs);

        /**
         * Отправляет одно heartbeat-сообщение к бэкенду Музыки, чтобы она не разорвала с нами соединение.
         * @throw RequestTimeoutException
         */
        void ping();

        /**
         * Выполняет периодический вызов ping().
         */
        void pingLoop();

        /**
         * Устанавливает соединение с бэкендом Музыки, выполняет авторизацию (если возможно) и засыпает.
         * Просыпается от нотификации connectionCondVar_.
         */
        void connectionLoop();

        /**
         * Устанавливает соединение с бэкендом Музыки.
         * Блокирует исполнение, пока соединение не будет установлено.
         */
        void tryConnect();

        /**
         * Выполняет авторизацию в бэкенде Музыки, если есть данные для авторизации.
         * Блокирует исполнение, пока авторизация не будет выполнена, либо соединение не будет разорвано.
         */
        void tryAuth();

        /**
         * Отправить websocket-сообщение в несколько попыток через YandexMusic::requestTry(...)
         * @param json Тело сообщения
         * @param startRequestTs time_point, относительно которого следим за таймаутом totalRequestTimeoutMs_
         * @return Json с ответом на запрос.
         * @throw AuthorizationFailedException
         * @throw RequestTimeoutException
         */
        Json::Value request(Json::Value& json, const std::chrono::steady_clock::time_point& startRequestTs);

        /**
         * Отправить websocket-сообщение в бэкенд Музыки и синхронно дождаться ответа на него.
         * @param json Тело сообщения
         * @param requestId id запроса
         * @param action событие для которого выполняется запрос
         * @param tryCount номер попытки
         * @param startRequestTs time_point, относительно которого следим за таймаутом totalRequestTimeoutMs_
         * @return Json с ответом на запрос, или Json::nullValue, если нужно попытаться еще раз
         * @throw AuthorizationFailedException
         * @throw RequestTimeoutException
         */
        Json::Value requestTry(Json::Value& json, const std::string& requestId, const std::string& action,
                               int tryCount, const std::chrono::steady_clock::time_point& startRequestTs);

        std::shared_ptr<const YandexIO::LatencyData> measureRequestStart(const std::string& action);
        /**
         * Отправить в метрику событие для замера времени выполнения запроса
         * @param action событие для которого выполняется запрос
         * @param latencyPoint точка отсчета, получается вызовом measureRequestStart
         */
        void measureRequestFinish(const std::string& action, std::shared_ptr<const YandexIO::LatencyData>& latencyPoint);

        /**
         * Бросает quasar::RequestTimeoutException, если разница между текущим временем и startRequestTimestamp превышает
         * максимально возможное время запроса.
         * @param action событие для которого выполняется запрос
         * @param startRequestTimestamp Время начала выполнения запроса
         */
        void checkRequestTimeout(const std::string& action, const std::chrono::steady_clock::time_point& startTimestamp);

        /**
         * Блокирует исполнение, пока не будет выполнена авторизация в бэкенде Музыки.
         * Если авторизация уже выполнена, то ничего не делает.
         *
         * Этот метод генерирует std::logic_error, если вызван из connectionThread, так как авторизация
         * происходит только в connectionThread_.
         */
        void waitUntilAuthorized(std::chrono::milliseconds timeout);

        /**
         * Блокирует исполнение, пока не будет установлено соединение с бэкендом Музыки.
         * Если соединение уже установлено, то ничего не делает.
         *
         * Этот метод генерирует std::logic_error, если вызван из connectionThread, так как коннект
         * происходит только в connectionThread_.
         */
        void waitUntilConnected(std::chrono::milliseconds timeout);

        /**
         * Разорвать соединение с бэкендом Музыки.
         */
        void disconnect();

        /**
         * Обработчик нового соединения с бэкендом Музыки.
         */
        void onConnect();

        /**
         * Обработчик сообщения от сервера.
         */
        void onMessage(const std::string& msg);

        /**
         * Обработчик разрыва соединения с бэкендом Музыки.
         */
        void onDisconnect(const Websocket::ConnectionInfo& connectionInfo);

        /**
         * Обработчик ошибок соединения с бэкендом Музыки.
         */
        void onFail(const Websocket::ConnectionInfo& connectionInfo);

        /**
         * Обрабатывает текущее состояние шота.
         * @param isInitial показывает, является ли эта проверка проверкой при начале проигрывания/при смене плейлиста
         */
        void checkForShot(bool isInitial = false);

        static bool updateRetryDelayCounter(quasar::RetryDelayCounter& delay, const Json::Value& config);

        static std::string parseArtists(const Json::Value& artists);
        static std::string parseGenreFromAlbums(const Json::Value& albums);

        /**
         * Действия, для совершения которых авторизация не нужна.
         */
        const std::set<std::string> UNAUTHORIZED_ACTIONS = {"ping", "auth"};

        std::shared_ptr<YandexIO::IDevice> device_;

        WebsocketClient websocketClient_;
        WebsocketClient::Settings wsSettings_;
        bool customApiUrl_ = false;
        std::chrono::milliseconds wsRequestTimeoutMs_{WS_REQUEST_TIMEOUT_MS};
        std::chrono::milliseconds totalRequestTimeoutMs_{TOTAL_REQUEST_TIMEOUT_MS};
        int retriesCount_ = RETRIES_COUNT;
        int retriesCountBeforeReconnect_ = RETRIES_COUNT_BEFORE_RECONNECT;

        bool shouldRetryPing_ = SHOULD_RETRY_PING;
        bool generateRetryId_ = GENERATE_RETRY_ID;
        int pingIntervalSec_ = PING_INTERVAL_SEC;
        int promisesLeakLimit_ = PROMISES_LEAK_LIMIT;

        bool requestLowBitrate_{false};
        std::string sessionId_;
        std::atomic<bool> stopped_;
        std::thread pingThread_;
        std::thread connectionThread_;

        quasar::SteadyConditionVariable connectionCondVar_;
        quasar::SteadyConditionVariable pingCondVar_;

        /**
         * Локальная очередь воспроизведения. Обновляется в результате вызова sync(). Каждый трек содержит
         * метаинформацию о песне, исполнителе, альбоме и т.д., а так же _может_ содержать url трека.
         */
        std::vector<std::shared_ptr<Track>> playlist_;

        /**
         * Информация об очереди воспроизведения. Обновляется в результате вызова sync().
         * Содержит метаинформацию о плейлисте.
         */
        PlaylistInfo playlistInfo_ = {};

        /**
         * Индекс текущей песни в очереди playlist_.
         */
        int index_ = 0;

        /**
         * Прогресс проигрывания в текущем треке в секундах.
         */
        unsigned int currentPositionSec_ = 0;

        /**
         * Продолжительность текущего трека в секундах.
         */
        unsigned int currentDurationSec_ = 0;

        /**
         * Словарь реквестов, ответов на которые мы еще ждем.
         * Ключ - regId, значение - промис с телом ответа.
         */
        std::map<std::string, std::unique_ptr<std::promise<Json::Value>>> requestPromises_;

        /**
         * Общий счетчик для попыток соединения и авторизации в бэкенде музыки.
         * Изменяется и читается только из connectionThread_.
         * Сбрасывается, когда пользователь авторизовался или изменился авторизационный токен.
         * См. https://st.yandex-team.ru/QUASAR-6986
         */
        quasar::RetryDelayCounter connectionDelayCounter_;

        std::mutex pingMutex_;
        mutable std::mutex playlistMutex_;
        std::mutex requestMutex_;
        std::mutex connectionMutex_;
        std::mutex onMessageMutex_;
        std::atomic<bool> isConnected_;
        std::atomic<bool> isAuthorized_;
        std::atomic<bool> needSync_;
        std::atomic<bool> ignoreNextEnd_{false};

        OnAuthFailedHandler onAuthFailedHandler_;

        std::string token_;
        std::string uid_;
        std::string deviceId_;

        std::atomic<double> normalizationTargetLufs_;
        std::string forcedTrackUrl_;

        std::shared_ptr<Track> shot_; // if now playing shot - it is here
        std::shared_ptr<const YandexIO::LatencyData> playerReconnectLatencyPoint_;
    };
} // namespace quasar
