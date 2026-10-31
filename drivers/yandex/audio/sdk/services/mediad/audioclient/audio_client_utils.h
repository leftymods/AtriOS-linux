#pragma once

#include <yandex_io/protos/model_objects.pb.h>
#include <yandex_io/protos/quasar_proto.pb.h>

#include <string>
#include <string_view>

namespace quasar {
    namespace AudioClientUtils {
        static constexpr const char* VOICE_TYPE = "voice";
        static constexpr const char* AUDIO_TYPE = "audio";
        static constexpr const char* FILE_PLAYER = "file_player";
        static constexpr const char* PCM_STREAM = "pcm_stream";

        std::string getPlayerType(const proto::AudioPlayerDescriptor& descriptor);

        std::string buildLogTag(const proto::AudioPlayerDescriptor& descriptor);

        std::string buildLogTagWithVsid(const proto::AudioPlayerDescriptor& descriptor, std::string_view vsid, const void* player);

        std::string getPlayerKey(const proto::AudioPlayerDescriptor& descriptor);

        std::string audioFormatToString(proto::Audio::Format format);

        bool isValidProgress(int position, int duration);

        std::string setVSIDToUrlParams(const std::string& url, const std::string& vsid);

    } // namespace AudioClientUtils
} // namespace quasar
