#pragma once

#include <yandex_io/modules/audio_input/controller/i_audio_input_controller.h>

#include <yandex_io/modules/audio_input/mics_state/i_mics_state_listener.h>

#include <yandex_io/sdk/backend_config_observer.h>
#include <yandex_io/sdk/sdk_interface.h>
#include <yandex_io/sdk/sdk_state_observer.h>

#include <memory>
#include <mutex>

namespace YandexIO {

    class MuteOnUpdateMicsStateProxy
        : public YandexIO::IMicsStateListener,
          public YandexIO::BackendConfigObserver,
          public YandexIO::SDKStateObserver {
    public:
        explicit MuteOnUpdateMicsStateProxy(std::weak_ptr<YandexIO::IAudioInputController> controller);

        void onMuted() override;
        void onUnmuted() override;

        void subscribeToSystemConfig(YandexIO::SDKInterface& sdk);
        void onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) override;

        void onSDKState(const YandexIO::SDKState& state) override;

    private:
        void muteController();
        void unmuteController();

    private:
        std::mutex mutex_;
        std::weak_ptr<YandexIO::IAudioInputController> controller_;
        bool doNotMuteOnUpdate_{false};
        bool forciblyMuted_{false};
        bool muted_{false};
        YandexIO::SDKState::UpdateState::State updateState_{YandexIO::SDKState::UpdateState::State::NONE};
    };

} // namespace YandexIO
