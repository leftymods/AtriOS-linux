#include "audio_writer.h"

using namespace quasar;

AudioWriter::~AudioWriter() {
}

void AudioWriter::tryRecover() {
}

std::string AudioWriter::getError() const {
    return "Unknown error";
}
