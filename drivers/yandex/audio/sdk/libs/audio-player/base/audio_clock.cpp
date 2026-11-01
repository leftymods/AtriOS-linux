#include "audio_clock.h"

namespace quasar {

    const char* AudioClock::syncLevelToText(AudioClock::SyncLevel syncLevel)
    {
        switch (syncLevel) {
            case SyncLevel::NONE:
                return "NONE";
            case SyncLevel::WEAK:
                return "WEAK";
            case SyncLevel::STRONG:
                return "STRONG";
        }
        return "UNDEFINED";
    }

} // namespace quasar
