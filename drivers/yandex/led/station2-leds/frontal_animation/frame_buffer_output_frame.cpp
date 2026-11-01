#include "frame_buffer_output_frame.h"

#include <utility>

FrameBufferOutputFrame::FrameBufferOutputFrame(Frame frame)
    : frame_(std::move(frame))
{
}

void FrameBufferOutputFrame::draw(YandexStationFrontalController* controller) const {
    controller->drawBuffer(frame_);
}

// default frame
FrameBufferOutputFrame::FrameBufferOutputFrame()
    : frame_(YandexStationFrontalController::HEIGHT * YandexStationFrontalController::WIDTH)
{
    for (int i = 0; i < YandexStationFrontalController::HEIGHT * YandexStationFrontalController::WIDTH; i++) {
        frame_[i] = BGR24{BGR_BLACK, BGR_BLACK, BGR_BLACK};
    }
}
