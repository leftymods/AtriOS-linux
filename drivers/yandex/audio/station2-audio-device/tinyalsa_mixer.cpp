#include "tinyalsa_mixer.h"

#include <yandex_io/libs/logging/logging.h>

#include <yandex_io/sdk/audio_source/feedback_source.h>

#include <contrib/libs/tinyalsa/include/tinyalsa/mixer.h>

#include <sstream>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

namespace {
    const std::string LOOPBACK_ENABLE_CONTROL_NAME = "Loopback Enable";
    const std::string LOOPBACK_ENABLE_CONTROL_VALUE_ENABLE = "Enable";
    const std::string LOOPBACK_ENABLE_CONTROL_VALUE_DISABLE = "Disable";

    const std::string LOOPBACK_SOURCE_CONTROL_NAME = "Loopback tmdin lb source";

    const char* loopbackSourceControlValue(YandexIO::FeedbackSource source) {
        switch (source) {
            case YandexIO::FeedbackSource::HW:
                return "TDMIN_A";
            case YandexIO::FeedbackSource::SW:
                return "TDMOUT_A";
            case YandexIO::FeedbackSource::HDMI:
                return "TDMOUT_C";
            default:
                YIO_LOG_ERROR_EVENT("TinyAlsaMixer.UnknownLoopbackSource", "Loopback source is unknown");
                throw std::runtime_error("Loopback source is unknown");
        }
    }
} // namespace

namespace quasar {
    namespace detail {

        TinyAlsaControlEnum::TinyAlsaControlEnum(mixer* mixer, std::string_view name)
            : name_(name)
            , control_(mixer_get_ctl_by_name(mixer, name_.c_str()))
        { // NULL safe
            if (control_) {
                YIO_LOG_INFO("Succesfully got mixer control '" << name_ << "'");
            } else {
                YIO_LOG_ERROR_EVENT("TinyAlsaControlEnum.ControlCaptureFailed", "Control '" << name_ << "' was not captured");
            }
        }

        void TinyAlsaControlEnum::setValue(const char* value) {
            if (control_) {
                const auto retcode = mixer_ctl_set_enum_by_string(control_, value);
                if (retcode != 0) {
                    std::ostringstream err;
                    err << "Tinyalsa mixer control '" << name_
                        << "' failed to set value '" << value
                        << "' with error code '" << retcode;
                    if (retcode == -EINVAL) {
                        err << " Return code indicated invalid value";
                    }
                    YIO_LOG_ERROR_EVENT("TinyAlsaControlEnum.ControlSetFailed", err.str());
                    return;
                }
                YIO_LOG_INFO("Tinyalsa mixer control '" << name_ << "' value was set to '" << value << "'");
            } else {
                YIO_LOG_WARN("Tinyalsa mixer control '" << name_ << "' can't be set, it was not initialized");
            }
        }
    } // namespace detail

    TinyAlsaMixer::TinyAlsaMixer()
        : alsaMixer_(mixer_open(0))
        , loopbackControl_(alsaMixer_, LOOPBACK_ENABLE_CONTROL_NAME)
        , loopbackSourceControl_(alsaMixer_, LOOPBACK_SOURCE_CONTROL_NAME)
    {
        if (!alsaMixer_) {
            YIO_LOG_ERROR_EVENT("TinyAlsaMixer.MixerOpenFailed", "Mixer 0 was not opened properly");
        }
    }

    TinyAlsaMixer::~TinyAlsaMixer() {
        mixer_close(alsaMixer_); // NULL safe
    }

    void TinyAlsaMixer::enableHardwareSync() {
        loopbackControl_.setValue(LOOPBACK_ENABLE_CONTROL_VALUE_ENABLE.c_str());
    }

    void TinyAlsaMixer::disableHardwareSync() {
        loopbackControl_.setValue(LOOPBACK_ENABLE_CONTROL_VALUE_DISABLE.c_str());
    }

    void TinyAlsaMixer::setLoopbackSource(YandexIO::FeedbackSource source) {
        loopbackSourceControl_.setValue(loopbackSourceControlValue(source));
    }

} // namespace quasar
