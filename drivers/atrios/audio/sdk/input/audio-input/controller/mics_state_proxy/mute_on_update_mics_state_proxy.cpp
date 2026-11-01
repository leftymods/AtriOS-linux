#include "mute_on_update_mics_state_proxy.h"

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

YIO_DEFINE_LOG_MODULE("mics_state");

using namespace YandexIO;

namespace {

    const std::string CONFIG_SECTION = "doNotMuteOnUpdate";

} // namespace

MuteOnUpdateMicsStateProxy::MuteOnUpdateMicsStateProxy(std::weak_ptr<IAudioInputController> controller)
    : controller_(std::move(controller))
{
}

void MuteOnUpdateMicsStateProxy::onMuted() {
    std::scoped_lock lock(mutex_);
    muted_ = true;
    if (!forciblyMuted_) {
        muteController();
    }
}

void MuteOnUpdateMicsStateProxy::onUnmuted() {
    std::scoped_lock lock(mutex_);
    muted_ = false;
    if (!forciblyMuted_) {
        unmuteController();
    }
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
void MuteOnUpdateMicsStateProxy::subscribeToSystemConfig(YandexIO::SDKInterface& sdk) {
    sdk.subscribeToSystemConfig(CONFIG_SECTION);
}

void MuteOnUpdateMicsStateProxy::onSystemConfig(const std::string& configName, const std::string& jsonConfigValue) {
    if (configName != CONFIG_SECTION) {
        return;
    }

    Json::Value config;
    try {
        config = quasar::parseJson(jsonConfigValue);
    } catch (const Json::Exception& e) {
        YIO_LOG_ERROR_EVENT("MuteOnUpdateMicsStateProxy.InvalidBackendConfigJson", "Can't parse " << CONFIG_SECTION << " config : " << e.what());
        return;
    }

    std::scoped_lock lock(mutex_);
    doNotMuteOnUpdate_ = config.isBool() && config.asBool();
}

void MuteOnUpdateMicsStateProxy::onSDKState(const SDKState& state) {
    if (state.updateState.state == updateState_) {
        return;
    }
    updateState_ = state.updateState.state;

    std::scoped_lock lock(mutex_);
    if (updateState_ == SDKState::UpdateState::State::NONE || !state.updateState.isCritical) {
        if (forciblyMuted_) {
            forciblyMuted_ = false;
            if (!muted_) {
                YIO_LOG_INFO("Unmute microphones after they have been forcibly muted");
                unmuteController();
            }
        }
    } else if (!doNotMuteOnUpdate_ && !forciblyMuted_) {
        forciblyMuted_ = true;
        if (!muted_) {
            YIO_LOG_INFO("Forcibly mute microphones during update");
            muteController();
        }
    }
}

void MuteOnUpdateMicsStateProxy::muteController() {
    if (auto controller = controller_.lock()) {
        controller->mute();
    }
}

void MuteOnUpdateMicsStateProxy::unmuteController() {
    if (auto controller = controller_.lock()) {
        controller->unmute();
    }
}
