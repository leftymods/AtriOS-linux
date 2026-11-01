#include "socket_audio_source.h"

#include "audio_source_utils.h"

#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/protobuf_utils/debug.h>
#include <yandex_io/protos/quasar_proto.pb.h>
#include <yandex_io/protos/yandex_io.pb.h>

using namespace quasar;

namespace {
    YandexIO::RequestChannelType getRequestChannelType(const proto::IOChannelRequest& request) {
        if (request.has_request_main()) {
            return YandexIO::RequestChannelType::MAIN;
        }
        if (request.has_request_vqe()) {
            return YandexIO::RequestChannelType::VQE;
        }
        if (request.has_request_raw()) {
            return YandexIO::RequestChannelType::RAW;
        }
        if (request.has_request_all()) {
            return YandexIO::RequestChannelType::ALL;
        }
        /* should not happen */
        return YandexIO::RequestChannelType::MAIN;
    }
} // namespace

namespace YandexIO {
    SocketAudioSource::SocketAudioSource(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory)
        : server_(ipcFactory->createIpcServer("yio_audio"))
    {
        server_->setClientDisconnectedHandler([this](auto& connection) {
            handleClientDisconnected(connection);
        });
        server_->setMessageHandler([this](const auto& msg, auto& connection) {
            if (msg->has_io_channel_request()) {
                handleChannelRequest(msg->io_channel_request(), connection);
            }
        });
        server_->listenService();
    }

    void SocketAudioSource::pushDataImpl(ChannelsData data) {
        std::scoped_lock guard(mutex_);
        sendChannel(data, RequestChannelType::MAIN, [](const ChannelData& channel) {
            return channel.isForRecognition;
        });
        sendChannel(data, RequestChannelType::VQE, [](const ChannelData& channel) {
            return channel.type == ChannelData::Type::VQE;
        });
        sendChannel(data, RequestChannelType::RAW, [](const ChannelData& channel) {
            return channel.type == ChannelData::Type::RAW;
        });

        sendAll(data);
    }

    void SocketAudioSource::sendChannel(const ChannelsData& data, RequestChannelType type,
                                        SocketAudioSource::FindIfLambda lambda) {
        if (connections_.count(type) == 0) {
            return;
        }

        const auto requestedChannel = std::find_if(data.cbegin(), data.cend(), lambda);
        if (requestedChannel == data.cend()) {
            return;
        }

        auto msg = ipc::buildMessage([&](auto& msg) {
            auto ioAudioData = msg.mutable_io_audio_data();
            auto protoChannel = ioAudioData->add_channels();
            *protoChannel = quasar::convert(*requestedChannel);
        });

        sendMessageToHandlers(msg, type);
    }

    void SocketAudioSource::sendAll(const ChannelsData& data) {
        const auto type = RequestChannelType::ALL;
        if (connections_.count(type) == 0) {
            return;
        }

        auto msg = ipc::buildMessage([&](auto& msg) {
            auto ioAudioData = msg.mutable_io_audio_data();
            for (const auto& channel : data) {
                auto protoChannel = ioAudioData->add_channels();
                *protoChannel = quasar::convert(channel);
            }
        });

        sendMessageToHandlers(msg, type);
    }

    void SocketAudioSource::sendMessageToHandlers(const ipc::SharedMessage& msg, RequestChannelType type) {
        auto [beginConnections, endConnections] = connections_.equal_range(type);
        for (auto it = beginConnections; it != endConnections; ++it) {
            it->second->send(msg);
        }
    }

    void SocketAudioSource::handleChannelRequest(const proto::IOChannelRequest& msg, Connection& connection) {
        std::scoped_lock guard(mutex_);

        YIO_LOG_DEBUG("got channel request: " << quasar::shortUtf8DebugString(msg));
        auto sharedConnection = connection.share();
        removeConnectionUnlocked(sharedConnection);
        if (msg.has_unsubscribe()) {
            /* only remove handler if "unsubscribe" was called */
            return;
        }

        const auto type = getRequestChannelType(msg);
        connections_.emplace(type, std::move(sharedConnection));
    }

    void SocketAudioSource::removeConnectionUnlocked(const SharedConnection& connectionToRemove) {
        auto it = std::find_if(connections_.begin(), connections_.end(), [&](const auto& item) {
            return item.second == connectionToRemove;
        });
        /* Remove handler from map and save with new type (if it is requested) */
        if (it != connections_.end()) {
            connections_.erase(it);
        }
    }

    void SocketAudioSource::handleClientDisconnected(Connection& connection) {
        std::scoped_lock guard(mutex_);
        removeConnectionUnlocked(connection.share());
    }

} /* namespace YandexIO */
