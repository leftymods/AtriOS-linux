#include "gio_logging_resolver.h"

#include <yandex_io/libs/logging/logging.h>

using namespace quasar::gstreamer;

YIO_DEFINE_LOG_MODULE("gresolver");

G_DEFINE_TYPE(GLoggingResolver, g_logging_resolver, G_TYPE_RESOLVER)

namespace {

    enum {
        PROP_RESOLVER = 1
    };

    struct LookupAsyncData {
        GResolver* resolver;
        GAsyncReadyCallback callback;
        gpointer user_data;
    };

    void lookup_callback(
        GObject* /*source*/,
        GAsyncResult* result,
        gpointer user_data) {
        LookupAsyncData* data = (LookupAsyncData*)user_data;
        data->callback(G_OBJECT(data->resolver), result, data->user_data);
    }

    GList* lookup_by_name(
        GResolver* resolver,
        const gchar* hostname,
        GCancellable* cancellable,
        GError** error) {
        YIO_LOG_INFO("lookup_by_name: " << hostname);

        GLoggingResolver* self = G_LOGGING_RESOLVER(resolver);

        GError* tmpError = nullptr;
        GList* addresses = g_resolver_lookup_by_name(self->resolver, hostname, cancellable, &tmpError);

        if (tmpError) {
            if (!g_error_matches(tmpError, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                YIO_LOG_ERROR_EVENT("GResolver.LookupByName.Error", tmpError->message);
            }

            g_propagate_error(error, tmpError);
        }

        return addresses;
    }

    void lookup_by_name_async(
        GResolver* resolver,
        const gchar* hostname,
        GCancellable* cancellable,
        GAsyncReadyCallback callback,
        gpointer user_data) {
        YIO_LOG_INFO("lookup_by_name_async: " << hostname);

        GLoggingResolver* self = G_LOGGING_RESOLVER(resolver);

        LookupAsyncData* data = g_slice_new0(LookupAsyncData);
        data->resolver = G_RESOLVER(g_object_ref(resolver));
        data->callback = callback;
        data->user_data = user_data;

        g_resolver_lookup_by_name_async(self->resolver, hostname, cancellable, lookup_callback, data);
    }

    GList* lookup_by_name_finish(
        GResolver* resolver,
        GAsyncResult* result,
        GError** error) {
        GLoggingResolver* self = G_LOGGING_RESOLVER(resolver);

        GError* tmpError = nullptr;
        GList* addresses = g_resolver_lookup_by_name_finish(self->resolver, result, &tmpError);

        if (tmpError) {
            if (!g_error_matches(tmpError, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                YIO_LOG_ERROR_EVENT("GResolver.LookupByNameAsync.Error", tmpError->message);
            }

            g_propagate_error(error, tmpError);
        }

        LookupAsyncData* data = (LookupAsyncData*)g_async_result_get_user_data(result);
        g_object_unref(data->resolver);
        g_slice_free(LookupAsyncData, data);

        return addresses;
    }

    GList* lookup_by_name_with_flags(
        GResolver* resolver,
        const gchar* hostname,
        GResolverNameLookupFlags flags,
        GCancellable* cancellable,
        GError** error) {
        YIO_LOG_INFO("lookup_by_name_with_flags: " << hostname << " " << flags);

        GLoggingResolver* self = G_LOGGING_RESOLVER(resolver);

        GError* tmpError = nullptr;
        GList* addresses = g_resolver_lookup_by_name_with_flags(self->resolver, hostname, flags, cancellable, &tmpError);

        if (tmpError) {
            if (!g_error_matches(tmpError, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                YIO_LOG_ERROR_EVENT("GResolver.LookupByNameWithFlags.Error", tmpError->message);
            }

            g_propagate_error(error, tmpError);
        }

        return addresses;
    }

    void lookup_by_name_with_flags_async(
        GResolver* resolver,
        const gchar* hostname,
        GResolverNameLookupFlags flags,
        GCancellable* cancellable,
        GAsyncReadyCallback callback,
        gpointer user_data) {
        YIO_LOG_INFO("lookup_by_name_with_flags_async: " << hostname << " " << flags);

        GLoggingResolver* self = G_LOGGING_RESOLVER(resolver);

        LookupAsyncData* data = g_slice_new0(LookupAsyncData);
        data->resolver = G_RESOLVER(g_object_ref(resolver));
        data->callback = callback;
        data->user_data = user_data;

        g_resolver_lookup_by_name_with_flags_async(self->resolver, hostname, flags, cancellable, lookup_callback, data);
    }

    GList* lookup_by_name_with_flags_finish(
        GResolver* resolver,
        GAsyncResult* result,
        GError** error) {
        GLoggingResolver* self = G_LOGGING_RESOLVER(resolver);

        GError* tmpError = nullptr;
        GList* addresses = g_resolver_lookup_by_name_with_flags_finish(self->resolver, result, &tmpError);

        if (tmpError) {
            if (!g_error_matches(tmpError, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                YIO_LOG_ERROR_EVENT("GResolver.LookupByNameWithFlagsAsync.Error", tmpError->message);
            }

            g_propagate_error(error, tmpError);
        }

        LookupAsyncData* data = (LookupAsyncData*)g_async_result_get_user_data(result);
        g_object_unref(data->resolver);
        g_slice_free(LookupAsyncData, data);

        return addresses;
    }

    gchar* lookup_by_address(
        GResolver* resolver,
        GInetAddress* address,
        GCancellable* cancellable,
        GError** error) {
        gchar* addressString = g_inet_address_to_string(address);
        YIO_LOG_INFO("lookup_by_address: " << addressString);
        g_free(addressString);

        GLoggingResolver* self = G_LOGGING_RESOLVER(resolver);

        GError* tmpError = nullptr;
        gchar* name = g_resolver_lookup_by_address(self->resolver, address, cancellable, &tmpError);

        if (tmpError) {
            if (!g_error_matches(tmpError, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                YIO_LOG_ERROR_EVENT("GResolver.LookupByAddress.Error", tmpError->message);
            }

            g_propagate_error(error, tmpError);
        }

        return name;
    }

    void lookup_by_address_async(
        GResolver* resolver,
        GInetAddress* address,
        GCancellable* cancellable,
        GAsyncReadyCallback callback,
        gpointer user_data) {
        gchar* addressString = g_inet_address_to_string(address);
        YIO_LOG_INFO("lookup_by_address_async: " << addressString);
        g_free(addressString);

        GLoggingResolver* self = G_LOGGING_RESOLVER(resolver);

        LookupAsyncData* data = g_slice_new0(LookupAsyncData);
        data->resolver = G_RESOLVER(g_object_ref(resolver));
        data->callback = callback;
        data->user_data = user_data;

        g_resolver_lookup_by_address_async(self->resolver, address, cancellable, lookup_callback, data);
    }

    gchar* lookup_by_address_finish(
        GResolver* resolver,
        GAsyncResult* result,
        GError** error) {
        GLoggingResolver* self = G_LOGGING_RESOLVER(resolver);

        GError* tmpError = nullptr;
        gchar* name = g_resolver_lookup_by_address_finish(self->resolver, result, &tmpError);

        if (tmpError) {
            if (!g_error_matches(tmpError, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                YIO_LOG_ERROR_EVENT("GResolver.LookupByAddressAsync.Error", tmpError->message);
            }

            g_propagate_error(error, tmpError);
        }

        LookupAsyncData* data = (LookupAsyncData*)g_async_result_get_user_data(result);
        g_object_unref(data->resolver);
        g_slice_free(LookupAsyncData, data);

        return name;
    }

    GList* lookup_records(
        GResolver* resolver,
        const gchar* rrname,
        GResolverRecordType record_type,
        GCancellable* cancellable,
        GError** error) {
        YIO_LOG_INFO("lookup_records: " << rrname << " " << record_type);

        GLoggingResolver* self = G_LOGGING_RESOLVER(resolver);

        GError* tmpError = nullptr;
        GList* records = g_resolver_lookup_records(self->resolver, rrname, record_type, cancellable, &tmpError);

        if (tmpError) {
            if (!g_error_matches(tmpError, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                YIO_LOG_ERROR_EVENT("GResolver.LookupRecords.Error", tmpError->message);
            }

            g_propagate_error(error, tmpError);
        }

        return records;
    }

    void lookup_records_async(
        GResolver* resolver,
        const gchar* rrname,
        GResolverRecordType record_type,
        GCancellable* cancellable,
        GAsyncReadyCallback callback,
        gpointer user_data) {
        YIO_LOG_INFO("lookup_records_async: " << rrname << " " << record_type);

        GLoggingResolver* self = G_LOGGING_RESOLVER(resolver);

        LookupAsyncData* data = g_slice_new0(LookupAsyncData);
        data->resolver = G_RESOLVER(g_object_ref(resolver));
        data->callback = callback;
        data->user_data = user_data;

        g_resolver_lookup_records_async(self->resolver, rrname, record_type, cancellable, lookup_callback, data);
    }

    GList* lookup_records_finish(
        GResolver* resolver,
        GAsyncResult* result,
        GError** error) {
        GLoggingResolver* self = G_LOGGING_RESOLVER(resolver);

        GError* tmpError = nullptr;
        GList* records = g_resolver_lookup_records_finish(self->resolver, result, &tmpError);

        if (tmpError) {
            if (!g_error_matches(tmpError, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
                YIO_LOG_ERROR_EVENT("GResolver.LookupRecordsAsync.Error", tmpError->message);
            }

            g_propagate_error(error, tmpError);
        }

        LookupAsyncData* data = (LookupAsyncData*)g_async_result_get_user_data(result);
        g_object_unref(data->resolver);
        g_slice_free(LookupAsyncData, data);

        return records;
    }

} // namespace

static void g_logging_resolver_init(GLoggingResolver* /*gptr*/) {
}

static void g_logging_resolver_set_property(
    GObject* object,
    guint propertyId,
    const GValue* value,
    GParamSpec* /*pspec*/
) {
    GLoggingResolver* self = G_LOGGING_RESOLVER(object);

    switch (propertyId) {
        case PROP_RESOLVER:
            self->resolver = G_RESOLVER(g_value_dup_object(value));
            break;
        default:
            g_assert_not_reached();
    }
}

static void g_logging_resolver_class_init(GLoggingResolverClass* logging_class) {
    GObjectClass* object_class = G_OBJECT_CLASS(logging_class);

    object_class->set_property = g_logging_resolver_set_property;

    g_object_class_install_property(object_class, PROP_RESOLVER,
                                    g_param_spec_object(
                                        "resolver", "resolver", "GResolver instance",
                                        G_TYPE_RESOLVER, GParamFlags(G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE)));

    GResolverClass* resolver_class = G_RESOLVER_CLASS(logging_class);

    resolver_class->lookup_by_name = lookup_by_name;
    resolver_class->lookup_by_name_async = lookup_by_name_async;
    resolver_class->lookup_by_name_finish = lookup_by_name_finish;
    resolver_class->lookup_by_name_with_flags = lookup_by_name_with_flags;
    resolver_class->lookup_by_name_with_flags_async = lookup_by_name_with_flags_async;
    resolver_class->lookup_by_name_with_flags_finish = lookup_by_name_with_flags_finish;
    resolver_class->lookup_by_address = lookup_by_address;
    resolver_class->lookup_by_address_async = lookup_by_address_async;
    resolver_class->lookup_by_address_finish = lookup_by_address_finish;
    resolver_class->lookup_records = lookup_records;
    resolver_class->lookup_records_async = lookup_records_async;
    resolver_class->lookup_records_finish = lookup_records_finish;
}

GlibResolverSwitcher::GlibResolverSwitcher() {
    default_ = g_resolver_get_default();
    logging_ = G_RESOLVER(g_object_new(G_TYPE_LOGGING_RESOLVER, "resolver", default_, nullptr));
}

GlibResolverSwitcher::~GlibResolverSwitcher() {
    if (enabled_) {
        enableLoggingResolver(false);
    }

    g_object_unref(logging_);
    g_object_unref(default_);
}

void GlibResolverSwitcher::enableLoggingResolver(bool enable) {
    std::scoped_lock lock(mutex_);

    if (enabled_ == enable) {
        return;
    }

    g_resolver_set_default(enable ? G_RESOLVER(logging_) : G_RESOLVER(default_));
    enabled_ = enable;
}

GlibResolverSwitcher& GlibResolverSwitcher::instance() {
    static GlibResolverSwitcher instance;
    return instance;
}
