#include "file_mics_state_provider.h"

#include <yandex_io/libs/base/persistent_file.h>
#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

namespace YandexIO {

    FileMicsStateProvider::FileMicsStateProvider(std::string micsMutedFile)
        : micsMutedFile_(std::move(micsMutedFile))
    {
        areMicsMuted_.store(quasar::fileExists(micsMutedFile_));
        YIO_LOG_INFO("Mics are " << (areMicsMuted_.load() ? "muted" : "unmuted") << " on device start");
    }

    bool FileMicsStateProvider::areMicsMuted() const {
        return areMicsMuted_.load();
    }

    void FileMicsStateProvider::toggleMute() {
        if (areMicsMuted_.load()) {
            std::remove(micsMutedFile_.c_str());
            areMicsMuted_.store(false);
            notifyUnmuted();
        } else {
            quasar::PersistentFile flag(micsMutedFile_, quasar::PersistentFile::Mode::TRUNCATE);
            areMicsMuted_.store(true);
            notifyMuted();
        }
    }

} /* namespace YandexIO */
