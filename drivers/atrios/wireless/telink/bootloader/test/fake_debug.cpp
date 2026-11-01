#include <yandex_io/libs/logging/logging.h>

extern "C" {
    void logMessage(const char* msg) {
        YIO_LOG_INFO(msg);
    }
}
