#include "rcu_audio_device.h"

#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/protos/quasar_proto.pb.h>

using namespace quasar;

namespace {
    constexpr size_t DEFAULT_CHANNEL_BUFFER_CAPACITY_SEC = 3;
    constexpr size_t DEFAULT_AUDIO_SAMPLING_RATE = 16000;
} // namespace

RcuAudioDevice::RcuAudioDevice(const std::shared_ptr<quasar::ipc::IIpcFactory>& ipcFactory)
    : channelBufferSize_(DEFAULT_CHANNEL_BUFFER_CAPACITY_SEC * DEFAULT_AUDIO_SAMPLING_RATE)
    , activeTag_(tagToBuffer_.end())
    , rcuConnector_(ipcFactory->createIpcConnector("rcu_voiced"))
{
    rcuConnector_->setMessageHandler([this](const auto& msg) {
        handleQuasarMessage(msg);
    });
    rcuConnector_->setConnectHandler([this]() {
        std::scoped_lock guard(mutex_);
        // Quasar-App crashed, pretend like capture finished
        if (hasActiveTag()) {
            YIO_LOG_INFO("Connected to rcu_voiced");
            tagToBuffer_.erase(tagToBuffer_.begin(), activeTag_);
            tagToBuffer_.erase(std::next(activeTag_), tagToBuffer_.end()); // left only activeTag_
            activeTag_->second.finished = true;
            condVar_.notify_one();
        } else {
            tagToBuffer_.clear();
        }
    });

    rcuConnector_->connectToService();
}

bool RcuAudioDevice::hasActiveTag() const {
    return activeTag_ != tagToBuffer_.end();
}

void RcuAudioDevice::handleQuasarMessage(const ipc::SharedMessage& msg) {
    if (!msg->has_rcu_voice_event()) {
        return;
    }
    std::scoped_lock guard(mutex_);
    const auto& rcuEvent = msg->rcu_voice_event();
    const Tag tag = rcuEvent.tag();

    if (rcuEvent.has_stop_streaming()) {
        auto iter = tagToBuffer_.find(tag);
        if (iter != tagToBuffer_.end()) { // do not handle end of empty stream
            iter->second.finished = true;
            if (iter == activeTag_) { // do not notify about not active tag, condvar.wait only on active tag
                condVar_.notify_one();
            }
        };
        return;
    }

    if (!rcuEvent.has_voice_data()) {
        return;
    }

    const auto& voiceData = rcuEvent.voice_data();
    if (hasActiveTag() && tag < activeTag_->first) {
        // skip expired buffers
        return;
    }
    if (voiceData.size() < sizeof(int16_t)) {
        return;
    }
    saveChunk(tag, voiceData.data(), voiceData.size());
}

// should be call under mutex_
void RcuAudioDevice::saveChunk(Tag tag, const char* data, std::size_t dataSize) {
    auto [iter, inserted] = tagToBuffer_.emplace(tag, channelBufferSize_);
    // TODO: handle overflow
    iter->second.buffer.write((const int16_t*)data, dataSize / sizeof(int16_t));
    if (iter == activeTag_) {
        condVar_.notify_one();
    }
}

void RcuAudioDevice::setActiveTag(Tag tag) {
    std::scoped_lock guard(mutex_);
    auto [iter, inserted] = tagToBuffer_.emplace(tag, channelBufferSize_);
    activeTag_ = iter;
    tagToBuffer_.erase(tagToBuffer_.begin(), activeTag_); // remove expired buffers
    condVar_.notify_one();
}

RcuAudioDevice::Result RcuAudioDevice::capture(std::vector<int16_t>& data) {
    std::unique_lock lock(mutex_);
    condVar_.wait(lock, [this]() {
        if (!hasActiveTag()) {
            return false; // wait active tag
        }
        if (activeTag_->second.finished) {
            return true;
        }
        return activeTag_->second.buffer.getSize() > 0;
    });

    // entered here when activeTag_ is set
    Result res;
    res.tag = activeTag_->first;

    auto& rcuData = activeTag_->second;
    if (rcuData.finished) {
        tagToBuffer_.erase(activeTag_);
        activeTag_ = tagToBuffer_.end(); // drop active tag when finished

        res.status = Status::FINISHED;
        return res;
    }
    auto& ringBuffer = rcuData.buffer;
    data.resize(ringBuffer.getSize());
    ringBuffer.read(data.data(), data.size());
    ringBuffer.clear();

    res.status = Status::OK;
    return res;
}
