#pragma once

#include "i_toggle_mics_state_provider.h"

#include <atomic>
#include <string>

namespace YandexIO {

    class FileMicsStateProvider: public IToggleMicsStateProvider {
    public:
        explicit FileMicsStateProvider(std::string micsMutedFilePath);
        /**
         * @brief Update mics state (save it in file) and then send new state to listeners
         */
        void toggleMute() override;
        bool areMicsMuted() const override;

    private:
        const std::string micsMutedFile_;
        std::atomic_bool areMicsMuted_{false};
    };

} /* namespace YandexIO */
