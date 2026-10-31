#include "gstreamer_time_provider.h"

#include <gst/gst.h>
#include <gst/net/gstnetclientclock.h>
#include <gst/net/gstnettimeprovider.h>

#include <stdexcept>
#include <string>

namespace quasar {

    GstreamerTimeProvider::GstreamerTimeProvider(int port)
    {
        if (port <= 0) {
            throw std::invalid_argument("GstreamerTimeProvider: invalid argument \"port\": " + std::to_string(port));
        }

        auto gstClock = std::shared_ptr<GstClock>(gst_system_clock_obtain(), [](GstClock* p) { gst_object_unref(p); });
        if (!gstClock) {
            throw std::runtime_error("Fail to create time provider. Obtain system clock failed.");
        }

        gstNetTimeProvider_ = std::shared_ptr<GstNetTimeProvider>(
            gst_net_time_provider_new(gstClock.get(), nullptr, port),
            [](GstNetTimeProvider* p) { gst_object_unref(p); });
        if (!gstNetTimeProvider_) {
            throw std::runtime_error("Fail to create time provider.");
        }
    }

} // namespace quasar
