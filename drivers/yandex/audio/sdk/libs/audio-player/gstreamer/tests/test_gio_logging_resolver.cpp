#include <yandex_io/libs/audio_player/gstreamer/gio_logging_resolver.h>

#include <yandex_io/libs/logging/logging.h>

#include <yandex_io/tests/testlib/unittest_helper/unit_test_fixture.h>

#include <contrib/restricted/glib/gio/gio.h>

#include <library/cpp/testing/unittest/registar.h>

#include <future>
#include <iostream>

namespace {

    class LoggingResolverFixture: public QuasarUnitTestFixtureWithoutIpc {
    public:
        LoggingResolverFixture() {
            loop_ = g_main_loop_new(nullptr, false);

            loopThread_ = std::thread([this] {
                g_main_loop_run(loop_);
            });

            GResolver* defaultResolver = g_resolver_get_default();

            resolver_ = G_RESOLVER(g_object_new(G_TYPE_LOGGING_RESOLVER, "resolver", defaultResolver, nullptr));

            g_object_unref(defaultResolver);

            while (!g_main_loop_is_running(loop_)) {
                sleep(1);
            }
        }

        ~LoggingResolverFixture() {
            g_object_unref(resolver_);

            g_main_loop_quit(loop_);

            if (loopThread_.joinable()) {
                loopThread_.join();
            }

            g_main_loop_unref(loop_);
        }

        GResolver* resolver() {
            return resolver_;
        }

    private:
        GMainLoop* loop_;
        std::thread loopThread_;

        GResolver* resolver_;
    };

    template <class T, auto F>
    void lookupCallback(GObject* source, GAsyncResult* result, gpointer userData) {
        GResolver* resolver = G_RESOLVER(source);
        GError* error = nullptr;

        T value = F(resolver, result, &error);

        reinterpret_cast<std::promise<T>*>(userData)->set_value(value);
    }

} // namespace

Y_UNIT_TEST_SUITE(LoggingResolver) {
    Y_UNIT_TEST_F(testLookupByName, LoggingResolverFixture) {
        {
            GList* addresses = g_resolver_lookup_by_name(resolver(), "localhost", nullptr, nullptr);

            UNIT_ASSERT(addresses);

            g_resolver_free_addresses(addresses);
        }

        {
            GList* addresses = g_resolver_lookup_by_name(resolver(), "devnull", nullptr, nullptr);

            UNIT_ASSERT_EQUAL(addresses, nullptr);

            g_resolver_free_addresses(addresses);
        }
    }

    Y_UNIT_TEST_F(testLookupByNameWithFlags, LoggingResolverFixture) {
        GList* addresses = g_resolver_lookup_by_name_with_flags(resolver(), "localhost", G_RESOLVER_NAME_LOOKUP_FLAGS_IPV6_ONLY, nullptr, nullptr);

        UNIT_ASSERT(addresses);

        g_resolver_free_addresses(addresses);
    }

    Y_UNIT_TEST_F(testLookupByNameAsync, LoggingResolverFixture) {
        std::promise<GList*> addressesPromise;

        g_resolver_lookup_by_name_async(resolver(), "localhost", nullptr, lookupCallback<GList*, g_resolver_lookup_by_name_finish>, &addressesPromise);

        GList* addresses = addressesPromise.get_future().get();

        UNIT_ASSERT(addresses);

        g_resolver_free_addresses(addresses);
    }
}
