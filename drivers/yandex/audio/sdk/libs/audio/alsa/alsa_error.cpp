#include "alsa_error.h"

#include <alsa/asoundlib.h>

#include <string.h>

namespace {
    /* reentrant and a bit simplified version of snd_strerror() routine from contrib/restricted/alsa-lib/error.c */
    const char* snd_strerror_r(int errnum, char* buf, size_t buflen) {
        if (errnum < 0) {
            errnum = -errnum;
        }
        if (errnum < SND_ERROR_BEGIN) {
            // NOTICE: GNU version of strerror_r can return 'pointer to some (immutable) static string (in which case buf is unused)'
            //         By default there is POSIX variant. In case of switching to GNU there can be empty error messages.
            strerror_r(errnum, buf, buflen); // there is two versions of strerror_r, so return buf
            return buf;
        };
        errnum -= SND_ERROR_BEGIN;
        return errnum ? "Unknown error" : "Sound protocol is not compatible";
    }
} // namespace

namespace quasar {
    std::string alsaErrorTextMessage(int errNum) {
        char buf[1000] = {0};
        return std::to_string(errNum) + ':' + snd_strerror_r(errNum, buf, sizeof(buf));
    };
} // namespace quasar
