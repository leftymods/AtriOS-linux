#pragma once

#include <contrib/restricted/glib/gio/gio.h>

#include <mutex>

#define G_TYPE_LOGGING_RESOLVER (g_logging_resolver_get_type())
#define G_LOGGING_RESOLVER(o) (G_TYPE_CHECK_INSTANCE_CAST((o), G_TYPE_LOGGING_RESOLVER, GLoggingResolver))
#define G_LOGGING_RESOLVER_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), G_TYPE_LOGGING_RESOLVER, GLoggingResolverClass))
#define G_IS_LOGGING_RESOLVER(o) (G_TYPE_CHECK_INSTANCE_TYPE((o), G_TYPE_LOGGING_RESOLVER))
#define G_IS_LOGGING_RESOLVER_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE((k), G_TYPE_LOGGING_RESOLVER))
#define G_LOGGING_RESOLVER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS((o), G_TYPE_LOGGING_RESOLVER, GLoggingResolverClass))

extern "C" {
    G_BEGIN_DECLS

    struct GLoggingResolver {
        GResolver parent_instance;

        GResolver* resolver;
    };

    struct GLoggingResolverClass {
        GResolverClass parent_class;
    };

    GLIB_AVAILABLE_IN_ALL
    GType g_logging_resolver_get_type(void) G_GNUC_CONST;

    G_END_DECLS
}

namespace quasar::gstreamer {
    class GlibResolverSwitcher {
    public:
        void enableLoggingResolver(bool enable);

        static GlibResolverSwitcher& instance();

    private:
        GlibResolverSwitcher();
        ~GlibResolverSwitcher();

    private:
        void* default_;
        void* logging_;

        std::mutex mutex_;
        bool enabled_ = false;
    };
} // namespace quasar::gstreamer
