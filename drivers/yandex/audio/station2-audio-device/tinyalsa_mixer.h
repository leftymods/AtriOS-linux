#pragma once

#include <string>
#include <string_view>

struct mixer;
struct mixer_ctl;

namespace YandexIO {
    enum class FeedbackSource;
} // namespace YandexIO

namespace quasar {
    namespace detail {
        class TinyAlsaControlEnum {
        public:
            TinyAlsaControlEnum(mixer* mixer, std::string_view name);

            void setValue(const char* value);

        private:
            std::string name_;
            mixer_ctl* control_;
        };
    } // namespace detail

    class TinyAlsaMixer {
    public:
        TinyAlsaMixer();
        ~TinyAlsaMixer();

        void enableHardwareSync();
        void disableHardwareSync();
        void setLoopbackSource(YandexIO::FeedbackSource source);

    private:
        mixer* alsaMixer_;
        detail::TinyAlsaControlEnum loopbackControl_;
        detail::TinyAlsaControlEnum loopbackSourceControl_;
    };
} // namespace quasar
