#include "gstreamer.h"

#include "gio_logging_resolver.h"

#include <yandex_io/libs/logging/logging.h>

#include <contrib/restricted/spdlog/include/spdlog/spdlog.h>

#include <gst/gst.h>

#include <util/system/compiler.h>

#include <sstream>
#include <type_traits>

using namespace quasar;
using namespace quasar::gstreamer;

namespace {

    // ==== GLib logging ====

    struct GlibLogFields {
        const char* domain = nullptr;
        const char* message = nullptr;
        const char* function = nullptr;
        const char* file = nullptr;
        int line = 0;

        GlibLogFields(const GLogField* glibFields, gsize glibFieldsSize) {
            const auto isKey = [](const GLogField& field, const char* key) {
                return 0 == strcmp(field.key, key);
            };

            for (size_t i = 0; i < glibFieldsSize; ++i) {
                const auto& field = glibFields[i];

                // In GLogField, value may be either a text string or a binary string
                // * A text string must be a valid UTF-8 zero-terminated string, length must be set to -1
                // * A binary string must have non-negative length, all byte values are allowed
                //
                // For our logging purposes, we are only interested in text fields
                // (all the fields below must be encoded as text anyway)
                if (field.length >= 0) {
                    continue;
                }

                const char* text = static_cast<const char*>(field.value);
                if (isKey(field, "MESSAGE")) {
                    message = text;
                } else if (isKey(field, "CODE_FILE")) {
                    file = text;
                } else if (isKey(field, "CODE_LINE")) {
                    // there is no reasonable error handling to do here
                    // so line=0 in case of failure is OK
                    line = std::atoi(text);
                } else if (isKey(field, "CODE_FUNC")) {
                    function = text;
                } else if (isKey(field, "GLIB_DOMAIN")) {
                    domain = text;
                }
            }
        }
    };

    spdlog::level::level_enum mapGlibLogLevel(uint8_t level) {
        // Assertion failures are reported by glib at level WARNING,
        // promote them to ERROR to force sending to telemetry
        if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)) {
            return spdlog::level::err;
        } else if (level & (G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO)) {
            return spdlog::level::info;
        } else if (level & G_LOG_LEVEL_DEBUG) {
            return spdlog::level::debug;
        } else {
            return spdlog::level::off;
        }
    }

    GLogWriterOutput handleGlibDebugMessage(GLogLevelFlags glibLevel, const GLogField* fields, gsize countFields, gpointer /* user_data */) noexcept {
        auto logger = spdlog::default_logger();

        auto level = mapGlibLogLevel(glibLevel);
        if (level == spdlog::level::off || !logger->should_log(level)) {
            return G_LOG_WRITER_HANDLED;
        }

        GlibLogFields logFields(fields, countFields);
        std::ostringstream formattedMessage;
        if (logFields.domain) {
            formattedMessage << "[" << logFields.domain << "] ";
        }
        formattedMessage << logFields.message;
        if (logFields.function) {
            formattedMessage << " - " << logFields.function;
        }

        auto location = spdlog::source_loc{logFields.file ?: "", logFields.line, logFields.function ?: ""};
        logger->log(location, level, formattedMessage.str());

        return G_LOG_WRITER_HANDLED;
    }

    // ==== GStreamer logging ====

    spdlog::level::level_enum mapGstLogLevel(GstDebugLevel level) {
        switch (level) {
            case GST_LEVEL_ERROR:
                return spdlog::level::err;
            case GST_LEVEL_FIXME:
            case GST_LEVEL_WARNING:
                return spdlog::level::warn;

            case GST_LEVEL_INFO:
                return spdlog::level::info;

            case GST_LEVEL_DEBUG:
                return spdlog::level::debug;

            case GST_LEVEL_LOG:
            case GST_LEVEL_TRACE:
                return spdlog::level::trace;

            case GST_LEVEL_MEMDUMP:
            default:
                // ignore MEMDUMP due to memory limitations
                return spdlog::level::off;
        }
    }

    void handleGstDebugMessage(GstDebugCategory* category, GstDebugLevel gstLevel, const gchar* gFile, const gchar* gFunction, gint line, GObject* /* object */, GstDebugMessage* message, gpointer /* user_data */) noexcept {
        auto logger = spdlog::default_logger();

        auto level = mapGstLogLevel(gstLevel);
        if (level == spdlog::level::off || !logger->should_log(level)) {
            return;
        }

        const char* file = reinterpret_cast<const char*>(gFile);
        const char* function = reinterpret_cast<const char*>(gFunction);
        const char* categoryName = category ? category->name : nullptr;
        categoryName = categoryName ?: "unknown";

        auto location = spdlog::source_loc{file, line, function};

        const char* messageText = gst_debug_message_get(message);
        if (level == spdlog::level::err) {
            // as if YIO_LOG_ERROR_EVENT
            logger->log(location, level,
                        YIO_BUILD_LOG_EVENT_HEADER_IMPL("", "gstreamer", "GStreamer.Error.{0}") "[GST.{0}] {1} - {2}",
                        categoryName, messageText, function);
        } else {
            // as if YIO_LOG_WARN, etc...
            logger->log(location, level, "[GST.{}] {} - {}", categoryName, messageText, function);
        }
    }

} // namespace

Gstreamer::Gstreamer() {
    // XXX: This check is not thread-safe.
    // However, this should not really happen:
    // 1. double-initialization indicates malformed code, not a bad runtime condition
    // 2. reasonable higher-level code is expected to have a singleton-like initializer anyway
    if (gst_is_initialized()) {
        throw std::runtime_error("GStreamer is already initialized");
    }

    // Intercept GLib log messages (eg. asserts, errors)
    g_log_set_writer_func(&handleGlibDebugMessage, /* user_data = */ nullptr, /* user_data_free = */ nullptr);

    // Log handlers must be set before gst_init to capture logging from init procedure
    // This is the way to disable auto-adding default stderr log function in gst_init
    gst_debug_remove_log_function(gst_debug_log_default);

    // Intercept GStreamer debug log messages
    gst_debug_add_log_function(&handleGstDebugMessage, /* user_data = */ nullptr, /* notify = */ nullptr);

    if (!getenv("GST_DEBUG")) {
        YIO_LOG_INFO("Environment variable GST_DEBUG is not set. Setting GStreamer log level to FIXME (3).");
        gst_debug_set_default_threshold(GST_LEVEL_FIXME);
    }

    GError* err = nullptr;
    if (!gst_init_check(nullptr, nullptr, &err)) {
        std::ostringstream message;
        message << "Could not initialize GStreamer: " << (err ? err->message : "unknown error occurred");
        if (err) {
            g_error_free(err);
        }
        throw std::runtime_error(message.str());
    }
}

Gstreamer::~Gstreamer() {
    gst_deinit();

    gst_debug_remove_log_function(&handleGstDebugMessage);
    g_log_set_writer_func(g_log_writer_default, /* user_data = */ nullptr, /* user_data_free = */ nullptr);
}

std::shared_ptr<Gstreamer> quasar::gstreamer::ensureGstreamerInitialized() {
    static std::shared_ptr<Gstreamer> globalInstance;
    static std::mutex initMutex;

    auto lock = std::scoped_lock{initMutex};
    if (!globalInstance) {
        YIO_LOG_INFO("Initializing GStreamer");
        globalInstance = std::make_shared<Gstreamer>();
    }

    return globalInstance;
}
