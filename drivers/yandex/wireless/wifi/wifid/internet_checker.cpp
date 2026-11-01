#include "internet_checker.h"
#include "http_head.h"

#include <yandex_io/libs/http_client/http_client.h>
#include <yandex_io/libs/logging/logging.h>

namespace {

    class CurlInternetChecker: public quasar::IInternetChecker {
        quasar::HttpClient httpClient;
        const std::string url;

    public:
        CurlInternetChecker(const std::shared_ptr<YandexIO::IDevice>& device, const std::string& host, const std::string& path)
            : httpClient("blackhole", device)
            , url("http://" + host + path)
        {
            httpClient.disableHttpRequestMetrics(); // lot's of spam
            httpClient.setTimeout(std::chrono::milliseconds{2000});
        }

        Result check() override {
            Result result;
            try {
                auto httpResult = httpClient.head("generate-204", url);
                result.responseCode = httpResult.responseCode;
            } catch (const std::runtime_error& err) {
                // If we reach timeout we probably have incorrect DNS cache, so reset cache by setting 0 DNS timeout for the next request
                httpClient.setZeroDNSCacheTimeoutForNextRequest();
                throw(err);
            }
            return result;
        }
    };

    class GenericInternetChecker: public quasar::IInternetChecker {
        const std::string host;
        const std::string path;
        const std::string userAgent;

    public:
        GenericInternetChecker(const std::shared_ptr<YandexIO::IDevice>& device, std::string host, std::string path)
            : host(std::move(host))
            , path(std::move(path))
            , userAgent(device->getDeprecatedUserAgent())
        {
        }

        Result check() override {
            auto result = quasar::httpHead(host, path, userAgent);
            return {.responseCode = result.responseCode};
        }
    };
} // namespace

namespace quasar {
    std::unique_ptr<IInternetChecker> makeCurlInternetChecker(const std::shared_ptr<YandexIO::IDevice>& device, std::string host, std::string path) {
        return std::make_unique<CurlInternetChecker>(device, host, path);
    }

    std::unique_ptr<IInternetChecker> makeGenericInternetChecker(const std::shared_ptr<YandexIO::IDevice>& device, std::string host, std::string path) {
        return std::make_unique<GenericInternetChecker>(device, std::move(host), std::move(path));
    }
} // namespace quasar
