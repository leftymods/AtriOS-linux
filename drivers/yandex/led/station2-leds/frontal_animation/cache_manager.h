#pragma once

#include <yandex_io/libs/base/named_callback_queue.h>
#include <yandex_io/libs/telemetry/telemetry.h>

#include <util/folder/path.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

class CacheManager {
public:
    static const uint64_t DEFAULT_SIZE_ENTRIES;
    static const int DEFAULT_SIZE_KBYTES;
    static const int DEFAULT_CLEAR_CACHE_PERIOD_MINUTES;

    struct CacheResult {
        bool hit;
        std::string filename;
    };

    CacheManager(std::string version, uint64_t sizeEntries, int sizeKbytes,
                 int clearCachePeriodMinutes, std::string cacheLocationDir,
                 std::shared_ptr<YandexIO::ITelemetry> telemetry);

    CacheResult getFilename(const std::string& url);
    int clear();
    std::string updateCache(const std::string& url);
    void invalidateEntry(const std::string& url);
    static std::string transformUrl(const std::string& baseUrl);

    void setSizeEntries(uint64_t sizeEntries);
    void setSizeKbytes(int sizeKbytes);

private:
    const std::shared_ptr<YandexIO::ITelemetry> telemetry_;
    const TFsPath cacheLocationDir_;
    const std::string version_;
    uint64_t sizeEntries_;
    std::atomic<int> sizeKbytes_;
    const TFsPath cacheLocation_;
    const int clearCachePeriodMinutes_;

    std::deque<std::string> cache_;
    std::mutex cacheMutex_;

    quasar::NamedCallbackQueue callbackQueue{"CacheManager.clear"};

    void populateCache();

    void scheduleCacheClearing();
};
