#include "audio_client_utils.h"

#include <library/cpp/cgiparam/cgiparam.h>
#include <library/cpp/string_utils/url/url.h>

#include <sstream>

using namespace quasar;

std::string AudioClientUtils::getPlayerType(const proto::AudioPlayerDescriptor& descriptor) {
    switch (descriptor.type()) {
        case proto::AudioPlayerDescriptor::AUDIO:
            return AUDIO_TYPE;
        case proto::AudioPlayerDescriptor::VOICE:
            return VOICE_TYPE;
        case proto::AudioPlayerDescriptor::FILE_PLAYER:
            return FILE_PLAYER;
        case proto::AudioPlayerDescriptor::STREAM:
            return PCM_STREAM;
        default:
            return "unknown";
    }
}

std::string AudioClientUtils::buildLogTag(const proto::AudioPlayerDescriptor& descriptor) {
    std::stringstream ss;
    ss << "[" << getPlayerType(descriptor)
       << ", id=" << descriptor.player_id()
       << ", stream=" << descriptor.stream_id()
       << "] ";
    return ss.str();
}

std::string AudioClientUtils::buildLogTagWithVsid(const proto::AudioPlayerDescriptor& descriptor, std::string_view vsid, const void* player) {
    std::stringstream ss;
    ss << "[" << getPlayerType(descriptor)
       << ", id=" << descriptor.player_id()
       << ", stream=" << descriptor.stream_id()
       << ", vsid=" << vsid
       << ", player=" << player
       << "] ";
    return ss.str();
}

std::string AudioClientUtils::getPlayerKey(const proto::AudioPlayerDescriptor& descriptor) {
    if (descriptor.has_stream_id()) {
        return descriptor.stream_id();
    }
    return descriptor.player_id();
}

std::string AudioClientUtils::audioFormatToString(proto::Audio::Format format) {
    switch (format) {
        case proto::Audio::MP3:
            return "MP3";
        case proto::Audio::PCM_STREAM:
            return "PCM_STREAM";
        case proto::Audio::HLS:
            return "HLS";
        case proto::Audio::AUDIO_FILE:
            return "AUDIO_FILE";
        case proto::Audio::UNKNOWN:
            return "UNKNOWN";
    }
}

bool AudioClientUtils::isValidProgress(int position, int duration) {
    return duration == 0 && position >= 0                              // valid for radio streams
           || (duration > 0 && position >= 0 && position <= duration); // valid for music
}

std::string AudioClientUtils::setVSIDToUrlParams(const std::string& url, const std::string& vsid) {
    TStringBuf urlBase;
    TStringBuf query;
    TStringBuf fragment;
    SeparateUrlFromQueryAndFragment(url, urlBase, query, fragment);

    TCgiParameters params(query);

    params.Erase("vsid"); // remove old vsid if it was set
    params.InsertEscaped("vsid", vsid);

    std::stringstream ss;
    ss << urlBase << '?' << params.Print();
    if (!fragment.empty()) {
        ss << '#' << fragment;
    }

    return ss.str();
}
