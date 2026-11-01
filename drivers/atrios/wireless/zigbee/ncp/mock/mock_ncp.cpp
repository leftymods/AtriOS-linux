#include "mock_ncp.h"

using namespace zigbee;

MockNcp::MockNcp() {
    ON_CALL(*this, setListener).WillByDefault([this](std::weak_ptr<INcpListener> listener) {
        listener_ = listener;
    });

    ON_CALL(*this, init).WillByDefault([this]() {
        if (auto listener = listener_.lock()) {
            listener->onStateChanged(NcpState::Initialized);
        }
    });
}
