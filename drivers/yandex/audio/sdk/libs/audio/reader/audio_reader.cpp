#include "audio_reader.h"

using namespace quasar;

AudioReader::~AudioReader() {
}

void AudioReader::tryRecover() {
}

std::string AudioReader::getError() const {
    return "Unknown error";
}
