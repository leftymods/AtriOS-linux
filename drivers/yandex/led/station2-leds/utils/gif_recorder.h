#pragma once

#include "gif_utils.h"

namespace quasar {

    class GifRecorder {
    public:
        GifRecorder(const std::string& filename);

        void record(GifFrame&& frame);

        void save();

    private:
        const std::string filename_;

        Gif gif_;
    };

} // namespace quasar
