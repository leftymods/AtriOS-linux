#include "gif_recorder.h"

namespace quasar {

    GifRecorder::GifRecorder(const std::string& filename)
        : filename_(filename)
    {
    }

    void GifRecorder::record(GifFrame&& frame) {
        gif_.push_back(std::move(frame));
    }

    void GifRecorder::save() {
        saveGif(gif_, filename_);
    }

} // namespace quasar
