#pragma once

#include <yandex_io/sdk/private/device_context.h>
#include <yandex_io/libs/ipc/i_ipc_factory.h>

namespace YandexIO {

    class SpectrumProvider {
    public:
        explicit SpectrumProvider(const std::shared_ptr<quasar::ipc::IIpcFactory>& factory);
        void provideEvent(quasar::proto::IOEvent::Spectrum event);

    private:
        YandexIO::DeviceContext deviceContext_;
        std::mutex mutex_;
        std::optional<int64_t> activeMusicPlayer_;
        std::optional<int64_t> activeVoicePlayer_;
    };

} // namespace YandexIO
