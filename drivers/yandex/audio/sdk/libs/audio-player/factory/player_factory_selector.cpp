#include "player_factory_selector.h"

#include <yandex_io/libs/audio_player/gstreamer/gstreamer_audio_player.h>
#include <yandex_io/libs/audio_player/mock/mock_audio_player.h>

#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <string>

using namespace quasar;
using namespace quasar::gstreamer;

PlayerFactorySelector::PlayerFactorySelector(const Json::Value& serviceConfig)
{
    std::string player = "gstreamer";
    if (serviceConfig.isMember("playbackParams") && serviceConfig["playbackParams"].isObject()) {
        player = tryGetString(serviceConfig["playbackParams"], "player", "gstreamer");
    };
    YIO_LOG_DEBUG("Activating " << player << " player factory");
    if (player == "mock") {
        playerFactory_ = std::unique_ptr<AudioPlayerFactory>(new MockPlayerFactory());
    } else {
        playerFactory_ = std::unique_ptr<AudioPlayerFactory>(new GstreamerAudioPlayerFactory(ensureGstreamerInitialized()));
    }
    configUpdated(serviceConfig);
}

std::unique_ptr<AudioPlayer> PlayerFactorySelector::createPlayer(const AudioPlayer::Params& params) {
    return playerFactory_->createPlayer(params);
}

std::unique_ptr<AudioPlayer::Params> PlayerFactorySelector::createParams() {
    return playerFactory_->createParams();
};

void PlayerFactorySelector::configUpdated(const Json::Value& serviceConfig) {
    playerFactory_->configUpdated(serviceConfig);
}
