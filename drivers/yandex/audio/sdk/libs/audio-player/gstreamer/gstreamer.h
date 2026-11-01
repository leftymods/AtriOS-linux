#pragma once

#include <memory>

namespace quasar::gstreamer {

    // GStreamer global state guard.
    //
    // Holding a reference to Gstreamer is a proof that gstreamer library is initialized.
    class Gstreamer {
    public:
        // NOTE: `gst_init()` MUST NOT have been called at this moment,
        // otherwise `gst_init() + gst_deinit()` calls would be unbalanced.
        Gstreamer();
        ~Gstreamer();

        Gstreamer(const Gstreamer&) = delete;
        Gstreamer(Gstreamer&&) = delete;

        Gstreamer& operator=(const Gstreamer&) = delete;
        Gstreamer& operator=(Gstreamer&&) = delete;
    };

    std::shared_ptr<Gstreamer> ensureGstreamerInitialized();

} // namespace quasar::gstreamer
