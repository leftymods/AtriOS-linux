#pragma once
#include <memory>

namespace quasar {

    class GstreamerTimeProvider {
    public:
        explicit GstreamerTimeProvider(int port);

    private:
        std::shared_ptr<void> gstNetTimeProvider_;
    };

} // namespace quasar
