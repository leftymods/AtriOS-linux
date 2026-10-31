#pragma once

#include <yandex_io/interfaces/auth/i_auth_provider.h>

#include <yandex_io/libs/ipc/i_ipc_factory.h>
#include <yandex_io/libs/delay_timings_policy/delay_timings_policy.h>
#include <yandex_io/libs/device/device.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>

#include <mutex>
#include <optional>
#include <string>

namespace yandexstation_2 {
    enum class ReworkStatus {
        IN_PROGRESS,
        KEYS_RECEIVED,
        KEYS_BURNT,
        DONE
    };

    struct EncryptionKey {
        std::string value;
        std::string key_code;
    };

    struct EncryptionKeys {
        EncryptionKey widevine;
        EncryptionKey hdcp_14_tx;
        EncryptionKey hdcp_22_tx;
    };

    struct ReworkState {
        std::optional<EncryptionKeys> keys;
        ReworkStatus status = ReworkStatus::IN_PROGRESS;
    };

    class EncryptionKeyReworker {
        quasar::Lifetime lifetime_;
        std::shared_ptr<YandexIO::IDevice> device_;
        std::shared_ptr<quasar::IAuthProvider> authProvider_;
        std::atomic_bool isEncryptionKeyReworkRunning_{false}, reworkDone_{false};
        mutable std::mutex mutex_;
        quasar::SteadyConditionVariable sendRetriesWakeupVar_;
        quasar::BackoffRetriesWithRandomPolicy delayTimingsPolicy_;
        static std::optional<EncryptionKeys> parseKeys(const std::string& body);
        std::thread encryptionKeysReworkThread_;
        std::unique_ptr<YandexIO::DeviceCryptography> deviceCryptography_;
        const std::string reworkProvisioningMarkerFile_;
        const std::string teeProvisionerPath_;
        const std::string tempDir_;
        ReworkState getKeys(const ReworkState& state);
        ReworkState burnNewKeys(const ReworkState& state);
        ReworkState reportKeysSet(const ReworkState& state);
        void reworkThreadFunc();

    public:
        EncryptionKeyReworker(
            std::shared_ptr<YandexIO::IDevice> device,
            std::shared_ptr<quasar::IAuthProvider> authProvider,
            std::string reworkProvisioningMarkerFile,
            std::string teeProvisionerPath,
            std::string tempDir);
        ~EncryptionKeyReworker();
    };
} // namespace yandexstation_2
