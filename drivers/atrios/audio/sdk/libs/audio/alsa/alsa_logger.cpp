#include "alsa_logger.h"

#include <alsa/asoundlib.h>

#include <contrib/restricted/spdlog/include/spdlog/spdlog.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
    extern "C" void alsaErrorHandler(const char* file, int line, const char* function, int err [[maybe_unused]], const char* fmt, ...) {
        auto logger = spdlog::default_logger();
        if (!logger) {
            return;
        }

        char buf[1024] = "";
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        auto location = spdlog::source_loc{file, line, function};
        logger->log(location, spdlog::level::warn, "[ALSA] {}", buf);
    }

    class AlsaLoggerInit {
    public:
        AlsaLoggerInit() {
            snd_lib_error_set_handler(alsaErrorHandler);
        }
    };
} // namespace

void quasar::ensureAlsaLoggerInstalled() {
    static AlsaLoggerInit init;
}
