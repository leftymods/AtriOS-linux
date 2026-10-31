#include "socket_audio_source_client.h"

#include "audio_source_utils.h"

#include <yandex_io/protos/quasar_proto.pb.h>

using namespace quasar;

namespace YandexIO {
    SocketAudioSourceClient::SocketAudioSourceClient(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory)
        : connector_(ipcFactory->createIpcConnector("yio_audio"))
    {
        connector_->setMessageHandler([this](const auto& msg) {
            if (!msg->has_io_audio_data()) {
                return;
            }
            const auto& ioAudioData = msg->io_audio_data();
            ChannelsData data;
            for (const auto& protoChannel : ioAudioData.channels()) {
                data.push_back(convert(protoChannel));
            }

            for (auto& listener : listeners_) {
                if (auto slistener = listener.lock()) {
                    slistener->onAudioData(data);
                }
            }
        });
        connector_->setConnectHandler([this]() {
            std::unique_lock lock(typeMutex_);
            if (requestedType_.has_value()) {
                const auto type = requestedType_.value();
                lock.unlock();
                sendRequestChannel(type);
            }
        });
    }

    SocketAudioSourceClient::~SocketAudioSourceClient() {
        connector_->shutdown();
        connector_->waitUntilDisconnected();
    }

    void SocketAudioSourceClient::start() {
        connector_->connectToService();
    }

    void SocketAudioSourceClient::sendRequestChannel(RequestChannelType type) {
        auto msg = ipc::buildMessage([type](auto& msg) {
            auto request = msg.mutable_io_channel_request();
            switch (type) {
                case RequestChannelType::MAIN: {
                    request->mutable_request_main();
                    break;
                }
                case RequestChannelType::VQE: {
                    request->mutable_request_vqe();
                    break;
                }
                case RequestChannelType::RAW: {
                    request->mutable_request_raw();
                    break;
                }
                case RequestChannelType::ALL: {
                    request->mutable_request_all();
                    break;
                }
            }
        });
        connector_->sendMessage(msg);
    }

    void SocketAudioSourceClient::subscribeToChannels(RequestChannelType type) {
        {
            std::scoped_lock guard(typeMutex_);
            if (requestedType_.has_value() && requestedType_.value() == type) {
                return;
            }
            requestedType_ = type;
        }
        sendRequestChannel(type);
    }

    void SocketAudioSourceClient::unsubscribeFromChannels() {
        {
            std::scoped_lock guard(typeMutex_);
            if (!requestedType_.has_value()) {
                return;
            }
            requestedType_.reset();
        }
        auto msg = ipc::buildMessage([](auto& msg) {
            msg.mutable_io_channel_request()->mutable_unsubscribe();
        });
        connector_->sendMessage(msg);
    }

    void SocketAudioSourceClient::addListener(std::weak_ptr<Listener> listener) {
        listeners_.push_back(std::move(listener));
    }

} /* namespace YandexIO */
