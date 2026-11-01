#include "cache_manager.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <util/folder/path.h>

#include <regex>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

const uint64_t CacheManager::DEFAULT_SIZE_ENTRIES = 1000;
const int CacheManager::DEFAULT_SIZE_KBYTES = 100000;
const int CacheManager::DEFAULT_CLEAR_CACHE_PERIOD_MINUTES = 120;

namespace {

    int64_t calculateSizeOfFolder(const TFsPath& cacheLocation) {
        int64_t total = 0;

        TVector<TFsPath> files;
        cacheLocation.List(files);

        for (const auto& file : files) {
            total += quasar::getFileSize(file.GetPath());
        }

        return total;
    }

} // namespace

CacheManager::CacheManager(std::string version, uint64_t sizeEntries, int sizeKbytes, int clearCachePeriodMinutes,
                           std::string cacheLocationDir, std::shared_ptr<YandexIO::ITelemetry> telemetry)
    : telemetry_(std::move(telemetry))
    , cacheLocationDir_(cacheLocationDir)
    , version_(std::move(version))
    , sizeEntries_(sizeEntries)
    , sizeKbytes_(sizeKbytes)
    , cacheLocation_(JoinFsPaths(cacheLocationDir_, version_))
    , clearCachePeriodMinutes_(clearCachePeriodMinutes)
{
    cacheLocation_.MkDirs();
    populateCache();
    scheduleCacheClearing();
}

CacheManager::CacheResult CacheManager::getFilename(const std::string& url) {
    std::string filename = transformUrl(url);
    std::scoped_lock lock(cacheMutex_);
    auto const entry = find_if(cache_.begin(), cache_.end(), [&filename](const std::string& arg) {
        return arg == filename;
    });

    return CacheResult{entry != cache_.end(), JoinFsPaths(cacheLocation_.GetPath(), filename)};
}

std::string CacheManager::updateCache(const std::string& url) {
    std::string filename = transformUrl(url);
    std::scoped_lock lock(cacheMutex_);
    cache_.push_front(filename);
    if (cache_.size() > sizeEntries_) {
        cache_.pop_back();
    }
    return filename;
}

void CacheManager::invalidateEntry(const std::string& url) {
    std::string filename = transformUrl(url);
    std::scoped_lock lock(cacheMutex_);
    for (auto item = cache_.begin(); item != cache_.end(); item++) {
        if (*item == filename) {
            cache_.erase(item);
            return;
        }
    }
}

std::string CacheManager::transformUrl(const std::string& baseUrl) {
    std::string output;
    output = std::regex_replace(baseUrl, std::regex(R"(,)"), "  ");
    output = std::regex_replace(output, std::regex(R"(/)"), ",");
    output = std::regex_replace(output, std::regex(R"(\?)"), ", 1");
    output = std::regex_replace(output, std::regex(R"(\*)"), ", 2");

    return output;
}

void CacheManager::setSizeEntries(uint64_t sizeEntries) {
    std::scoped_lock lock(cacheMutex_);
    sizeEntries_ = sizeEntries;
}

void CacheManager::setSizeKbytes(int sizeKbytes) {
    sizeKbytes_ = sizeKbytes;
}

int CacheManager::clear() {
    {
        TVector<TString> cacheDirs;
        cacheLocationDir_.ListNames(cacheDirs);
        for (const auto& directory : cacheDirs) {
            if (directory != version_ && !directory.StartsWith(".")) {
                const auto path = cacheLocationDir_ / directory;
                YIO_LOG_INFO("removing old directory " << path.GetPath());
                telemetry_->reportEvent("animationCacheRemoveDirectory", path.GetPath());
                path.ForceDelete();
            }
        }
    }

    int deleted = 0;

    {
        TVector<TString> cacheFiles;
        cacheLocation_.ListNames(cacheFiles);
        for (const auto& file : cacheFiles) {
            std::scoped_lock lock(cacheMutex_);

            auto const entry = find_if(cache_.begin(), cache_.end(), [&file](const std::string& arg) {
                return arg == file;
            });

            if (entry == cache_.end() && !file.StartsWith(".")) {
                const auto fullpath = cacheLocation_ / file;
                YIO_LOG_INFO("removing file as old " << fullpath.GetPath());
                fullpath.ForceDelete();
                deleted += 1;
            }
        }
    }

    if (deleted) {
        std::stringstream number;
        number << deleted;
        telemetry_->reportEvent("animationCacheRemoveOldFile", number.str());
    }

    int sizeOfFolder = calculateSizeOfFolder(cacheLocation_);
    // TODO: check df too
    deleted = 0;
    const int targetSize = sizeKbytes_ * 1024;
    while (sizeOfFolder > targetSize) {
        std::scoped_lock lock(cacheMutex_);
        if (cache_.empty()) {
            YIO_LOG_ERROR_EVENT("CacheManager.EmptyCacheWhenDirectoryNotEmpty", "empty cache, but not directory");
            break;
        }
        auto entry = cache_.back();
        cache_.pop_back();
        TFsPath fullpath(JoinFsPaths(cacheLocation_, entry));
        YIO_LOG_INFO("cache size is " << sizeOfFolder << " bytes, limit is " << targetSize << " bytes");
        sizeOfFolder -= quasar::getFileSize(fullpath.GetPath());
        YIO_LOG_INFO("removing file as cache is full " << fullpath.GetPath());
        fullpath.ForceDelete();
        deleted += 1;
    }

    if (deleted) {
        telemetry_->reportEvent("animationCacheCleanPerformed", std::to_string(deleted));
    }
    return deleted;
}

void CacheManager::populateCache() {
    std::scoped_lock lock(cacheMutex_);

    TVector<TString> files;
    cacheLocation_.ListNames(files);

    for (const auto& file : files) {
        if (!file.StartsWith(".")) {
            cache_.push_front(file);
        }
        if (cache_.size() > sizeEntries_) {
            break;
        }
    }
    YIO_LOG_INFO("images cache populated, " << cache_.size() << " entries");
}

void CacheManager::scheduleCacheClearing() {
    callbackQueue.addDelayed([this] {
        try {
            clear();
        } catch (const std::exception& e) {
            YIO_LOG_ERROR_EVENT("CacheManager.Clear.Exception", "clear cache failed, " << e.what());
        }
        scheduleCacheClearing();
    }, std::chrono::minutes(clearCachePeriodMinutes_));
}
