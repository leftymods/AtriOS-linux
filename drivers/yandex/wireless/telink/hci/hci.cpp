#include "hci.h"
#include "hci_command.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/logging/logging.h>

using namespace telink;

namespace {

    constexpr uint8_t MSG_START_FLAG = 0x55;
    constexpr uint8_t MSG_END_FLAG = 0xAA;

    constexpr uint8_t MSG_HEADER_LENGTH = 6;
    constexpr uint8_t MSG_MAX_LENGTH = 64;

    constexpr auto HCI_TIMEOUT = std::chrono::milliseconds(100);

    enum class HciState {
        StartFlag,
        Header,
        Payload,
        Done,
    };

    struct HciHeader {
        uint16_t msgType;
        uint16_t msgLen;
        uint8_t checksum;

    public:
        bool deserialize(std::span<const uint8_t> payload) {
            if (payload.size() < MSG_HEADER_LENGTH) {
                return false;
            }

            if (payload[0] != MSG_START_FLAG) {
                return false;
            }

            msgType = (payload[1] << 8) | payload[2];
            msgLen = (payload[3] << 8) | payload[4];
            checksum = payload[5];

            if (MSG_HEADER_LENGTH + msgLen + 1 > MSG_MAX_LENGTH) {
                return false;
            }

            return true;
        }
    };

    struct HciAcknowledge: public HciCommand<0x8000> {
        uint16_t msgType;
        HciStatus status;
        uint8_t reserved = 0;

    public:
        std::vector<uint8_t> serialize() const {
            return {
                static_cast<uint8_t>(msgType << 8),
                static_cast<uint8_t>(msgType & 0xff),
                static_cast<uint8_t>(status),
                0,
            };
        }

        bool deserialize(std::span<const uint8_t> payload) {
            if (payload.size() != 4) {
                return false;
            }

            msgType = (payload[0] >> 8) | payload[1];
            status = static_cast<HciStatus>(payload[2]);
            return true;
        }
    };

    uint8_t calculateChecksum(uint16_t type, std::span<const uint8_t> payload) {
        uint8_t checksum = (type >> 8) ^ (type & 0xff) ^ (payload.size() >> 8) ^ (payload.size() & 0xff);

        for (uint8_t byte : payload) {
            checksum ^= byte;
        }

        return checksum;
    }

} // namespace

HciListener::~HciListener() = default;

Hci::Hci(const std::string& port)
    : tty_(std::make_unique<quasar::Tty>(port, quasar::Tty::Parameters()))
{
}

Hci::~Hci() {
    stop();
}

void Hci::start(std::shared_ptr<HciListener> listener) {
    listener_ = std::move(listener);

    stopped_ = false;

    thread_ = std::thread(&Hci::receiveLoop, this);

    YIO_LOG_INFO("HCI started");
}

void Hci::stop() {
    stopped_ = true;

    if (thread_.joinable()) {
        thread_.join();
    }

    listener_.reset();

    YIO_LOG_INFO("HCI stopped");
}

HciStatus Hci::send(uint16_t msgType, std::span<const uint8_t> payload) {
    if (MSG_HEADER_LENGTH + payload.size() + 1 > MSG_MAX_LENGTH) {
        throw std::runtime_error("Message is too large");
    }

    std::vector<uint8_t> data;
    data.reserve(MSG_MAX_LENGTH);

    data.push_back(MSG_START_FLAG);
    data.push_back(msgType >> 8);
    data.push_back(msgType & 0xff);
    data.push_back(payload.size() >> 8);
    data.push_back(payload.size() & 0xff);
    data.push_back(calculateChecksum(msgType, payload));

    data.insert(data.end(), payload.begin(), payload.end());

    data.push_back(MSG_END_FLAG);

    YIO_LOG_DEBUG("Sending message " << quasar::Hex(msgType) << " payload " << quasar::Hex(payload));

    if (msgType != HciAcknowledge::TYPE) {
        std::scoped_lock lock(sendMutex_);
        std::unique_lock ackLock(ackMutex_);

        msgStatus_.reset();

        tty_->write(data);

        bool ack = ackCv_.wait_for(
            ackLock,
            HCI_TIMEOUT,
            [this]() {
                return msgStatus_.has_value();
            });

        if (!ack) {
            throw std::runtime_error("Timeout");
        }

        return *msgStatus_;
    } else {
        std::unique_lock ackLock(ackMutex_);

        tty_->write(data);

        return HciStatus::Success;
    }
}

void Hci::receiveLoop() {
    HciState state = HciState::StartFlag;
    std::chrono::steady_clock::time_point timestamp;

    std::vector<uint8_t> buffer;
    buffer.reserve(MSG_MAX_LENGTH);

    HciHeader header;

    while (!stopped_) {
        switch (state) {
            case HciState::StartFlag: {
                if (auto byte = tty_->readByte()) {
                    if (*byte == MSG_START_FLAG) {
                        buffer.push_back(*byte);
                        state = HciState::Header;
                        timestamp = std::chrono::steady_clock::now();
                    }
                }

                break;
            }
            case HciState::Header: {
                auto data = tty_->read(MSG_HEADER_LENGTH - buffer.size());

                buffer.insert(buffer.end(), data.begin(), data.end());

                if (buffer.size() != MSG_HEADER_LENGTH) {
                    continue;
                }

                if (!header.deserialize(buffer)) {
                    YIO_LOG_WARN("Bad header: " << quasar::Hex(buffer));

                    state = HciState::Done;
                    continue;
                }

                state = HciState::Payload;

                break;
            }
            case HciState::Payload: {
                auto data = tty_->read(MSG_HEADER_LENGTH + header.msgLen + 1 - buffer.size());

                buffer.insert(buffer.end(), data.begin(), data.end());

                if (buffer.size() != MSG_HEADER_LENGTH + header.msgLen + 1) {
                    continue;
                }

                if (buffer.back() != MSG_END_FLAG) {
                    YIO_LOG_WARN("Bad message: " << quasar::Hex(buffer));

                    state = HciState::Done;
                    continue;
                }

                std::span<const uint8_t> payload{
                    buffer.begin() + MSG_HEADER_LENGTH,
                    header.msgLen,
                };

                uint8_t checksum = calculateChecksum(header.msgType, payload);

                if (header.checksum != checksum) {
                    YIO_LOG_WARN("Bad checksum: " << quasar::Hex(buffer));

                    state = HciState::Done;
                    continue;
                }

                YIO_LOG_DEBUG("Received message " << quasar::Hex(header.msgType) << " payload " << quasar::Hex(payload));

                handleMessage(header.msgType, payload);

                state = HciState::Done;

                break;
            }
            case HciState::Done: {
                buffer.clear();
                state = HciState::StartFlag;
                break;
            }
        }

        if (state != HciState::StartFlag) {
            const auto now = std::chrono::steady_clock::now();

            if (timestamp - now > HCI_TIMEOUT) {
                YIO_LOG_WARN("Receive timeout");

                state = HciState::Done;
            }
        }
    }
}

void Hci::handleMessage(uint16_t msgType, std::span<const uint8_t> payload) {
    if (msgType == HciAcknowledge::TYPE) {
        HciAcknowledge ack;

        if (!ack.deserialize(payload)) {
            YIO_LOG_WARN("Bad ACK: " << quasar::Hex(payload));
            return;
        }

        YIO_LOG_DEBUG("ACK " << quasar::Hex(ack.msgType) << " " << quasar::Hex(static_cast<uint8_t>(ack.status)));

        if (ack.msgType == HciAcknowledge::TYPE) {
            return;
        }

        std::scoped_lock ackLock(ackMutex_);
        msgStatus_ = ack.status;
        ackCv_.notify_one();

        return;
    }

    listener_->onCommand(msgType, payload);
}
