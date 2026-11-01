#include "encryption_key_reworker.h"

#include <yandex_io/libs/base/utils.h>
#include <yandex_io/libs/base/crc32.h>
#include <yandex_io/libs/http_client/http_client.h>
#include <yandex_io/libs/json_utils/json_utils.h>
#include <yandex_io/libs/logging/logging.h>

#include <cstdio>
#include <fstream>
#include <memory>

YIO_DEFINE_LOG_MODULE("yandexstation_2");

using namespace std::chrono_literals;

namespace yandexstation_2 {
    EncryptionKeyReworker::EncryptionKeyReworker(std::shared_ptr<YandexIO::IDevice> device,
                                                 std::shared_ptr<quasar::IAuthProvider> authProvider,
                                                 std::string reworkProvisioningMarkerFile,
                                                 std::string teeProvisionerPath,
                                                 std::string tempDir)
        : device_(std::move(device))
        , authProvider_(std::move(authProvider))
        , reworkProvisioningMarkerFile_(std::move(reworkProvisioningMarkerFile))
        , teeProvisionerPath_(std::move(teeProvisionerPath))
        , tempDir_(std::move(tempDir))
    {
        if (quasar::fileExists(reworkProvisioningMarkerFile_)) {
            return;
        }

        auto cryptographyConfig = quasar::getJson(device_->configuration()->getServiceConfig("common"), "cryptography");
        deviceCryptography_ = device_->hal()->createDeviceCryptography(cryptographyConfig);

        auto unixtime = std::chrono::system_clock::now().time_since_epoch();
        auto unixtimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(unixtime).count();

        uint32_t seed = (unixtimeMs + quasar::getCrc32(device_->deviceId())) & 0xFFFFFFFF;

        delayTimingsPolicy_ = quasar::BackoffRetriesWithRandomPolicy(seed);
        delayTimingsPolicy_.initCheckPeriod(1s, 1min, 100min);

        isEncryptionKeyReworkRunning_.store(true);
        encryptionKeysReworkThread_ = std::thread([this] { reworkThreadFunc(); });
    }

    EncryptionKeyReworker::~EncryptionKeyReworker() {
        lifetime_.die();
        isEncryptionKeyReworkRunning_.store(false);
        if (encryptionKeysReworkThread_.joinable()) {
            encryptionKeysReworkThread_.join();
        }
    }

    std::optional<EncryptionKeys> EncryptionKeyReworker::parseKeys(const std::string& body) {
        Json::Value parsed{};
        try {
            parsed = quasar::parseJson(body);
        } catch (const Json::Exception& exc) {
            YIO_LOG_ERROR_EVENT("EncryptionKeyReworker.ParseKeys.InvalidJson", "Can't parse keys info: " << exc.what());
            return std::nullopt;
        }

        EncryptionKeys keys{};
        if (parsed.isObject() && parsed.isMember("keys") && parsed["keys"].isArray()) {
            for (const auto& entry : parsed["keys"]) {
                if (!entry.isMember("type") || !entry.isMember("value")) {
                    continue;
                }

                if (const auto& type = entry["type"].asString(); type == "widevine") {
                    keys.widevine.value = quasar::base64Decode(entry["value"].asString());
                    keys.widevine.key_code = "0x11";
                } else if (type == "hdcp_14_tx") {
                    keys.hdcp_14_tx.value = quasar::base64Decode(entry["value"].asString());
                    keys.hdcp_14_tx.key_code = "0x31";
                } else if (type == "hdcp_22_tx") {
                    keys.hdcp_22_tx.value = quasar::base64Decode(entry["value"].asString());
                    keys.hdcp_22_tx.key_code = "0x32";
                } else {
                    YIO_LOG_ERROR_EVENT("EncryptionKeyReworker.UnknownKeyType", "Unknown key type: " << type);
                }
            }
        }

        if (keys.widevine.value.empty()) {
            YIO_LOG_ERROR_EVENT("EncryptionKeyReworker.WidevineKeyEmpty", "Widevine key is empty, whole answer considered invalid");
            return std::nullopt;
        }
        if (keys.hdcp_14_tx.value.empty()) {
            YIO_LOG_ERROR_EVENT("EncryptionKeyReworker.HDCP1_4KeyEmpty", "HDCP 1.4 tx key is empty, whole answer considered invalid");
            return std::nullopt;
        }
        if (keys.hdcp_22_tx.value.empty()) {
            YIO_LOG_ERROR_EVENT("EncryptionKeyReworker.HDCP2_2KeyEmpty", "HDCP 2.2 tx key is empty, whole answer considered invalid");
            return std::nullopt;
        }

        return keys;
    }

    ReworkState EncryptionKeyReworker::getKeys(const ReworkState& state) {
        ReworkState result = state;
        const auto backendUrl = device_->configuration()->getBackendUrl();
        const auto getKeysParams = "device_id=" + device_->deviceId() + "&platform=" + device_->configuration()->getDeviceType();
        const auto getKeysUrl = backendUrl + "/get_drm_keys?" + getKeysParams;
        const auto authToken = authProvider_->ownerAuthInfo().value()->authToken;
        quasar::HttpClient::Headers headers = {{"Authorization", "OAuth " + authToken}};
        try {
            const auto signature = deviceCryptography_->sign(getKeysParams);
            quasar::HttpClient::addSignatureHeaders(headers, signature, deviceCryptography_->getType());
        } catch (const std::exception& e) {
            YIO_LOG_ERROR_EVENT("EncryptionKeyReworker.GetKeys.SignFailed", "Can't sign urlParams (signature version 2): " << e.what());
            return result;
        }
        quasar::HttpClient client("quasar-backend", device_);
        client.setTimeout(std::chrono::milliseconds{60000});
        client.setRetriesCount(0);
        try {
            const auto response = client.get("get-drm-keys", getKeysUrl, headers);
            if (response.responseCode == 200) {
                YIO_LOG_DEBUG("Keys received: " << response.body);
                result.keys = parseKeys(response.body);
                if (result.keys) {
                    result.status = ReworkStatus::KEYS_RECEIVED;
                } else {
                    delayTimingsPolicy_.increaseDelayBetweenCalls();
                }
                return result;
            } else if (response.responseCode == 404) { // Keys are already set, stop working
                YIO_LOG_INFO("Unable to get device keys, according to spec it's already done: " << response.responseCode << ". Body: " << response.body);
                result.keys = std::nullopt;
                result.status = ReworkStatus::DONE;
                delayTimingsPolicy_.increaseDelayBetweenCalls();
                return result;
            } else {
                YIO_LOG_ERROR_EVENT("EncryptionKeyReworker.GetKeys.Non200Response", "Unable to get device keys, backend returned code : " << response.responseCode << ". Body: " << response.body);
                result.keys = std::nullopt;
                result.status = ReworkStatus::IN_PROGRESS;
                delayTimingsPolicy_.increaseDelayBetweenCalls();
                return result;
            }
        } catch (const std::exception& e) {
            YIO_LOG_INFO("Exception trying to get device keys: " << e.what());
            result.keys = std::nullopt;
            result.status = ReworkStatus::IN_PROGRESS;
            delayTimingsPolicy_.increaseDelayBetweenCalls();
            return result;
        }
    }

    ReworkState EncryptionKeyReworker::burnNewKeys(const ReworkState& state) {
        const std::string tempFilePath = tempDir_ + "/key_tmp";
        auto resultState = state;
        for (const auto& type : {state.keys->widevine, state.keys->hdcp_14_tx, state.keys->hdcp_22_tx}) {
            try {
                std::ofstream tmpFile(tempFilePath, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc);
                tmpFile << type.value;
            } catch (const std::exception& e) {
                // Should we transition to some broken state and stop attempts?
                delayTimingsPolicy_.increaseDelayBetweenCalls();
                YIO_LOG_ERROR_EVENT("EncryptionKeyReworker.BurnKeys.OpenFailed", "Burning key " << type.key_code << " failed, unable to open temp file: " << e.what());
                return state;
            }
            const auto command = teeProvisionerPath_ + " -t " + type.key_code + " -i " + tempFilePath + " 2>&1";
            auto output = quasar::executeWithOutput(command.c_str());
            size_t pos = output.find('\n');
            while (pos != std::string::npos) {
                output.replace(pos, 1, "\\n");
                pos = output.find('\n', 2);
            };

            const bool isSuccess = output.find("[PROVISION-CA] store key success") != std::string::npos;
            YIO_LOG_INFO(command << " output: " << output);
            remove(tempFilePath.c_str());
            if (!isSuccess) {
                YIO_LOG_ERROR_EVENT("EncryptionKeyReworker.BurnKeys.BurnFailed", "Burning key " << type.key_code << " failed");
                // Keys are probably broken, go back to start, increasing the delay between requests
                delayTimingsPolicy_.increaseDelayBetweenCalls();
                resultState.status = ReworkStatus::IN_PROGRESS;
                resultState.keys = std::nullopt;
                return state;
            }
        }

        resultState.status = ReworkStatus::KEYS_BURNT;
        return resultState;
    }

    ReworkState EncryptionKeyReworker::reportKeysSet(const ReworkState& state) {
        auto resultState = state;
        const auto backendUrl = device_->configuration()->getBackendUrl();
        const auto confirmDrmKeysSetParams = "device_id=" + device_->deviceId() + "&platform=" + device_->configuration()->getDeviceType();
        const auto confirmDrmKeysSetUrl = backendUrl + "/confirm_drm_keys_setting?" + confirmDrmKeysSetParams;
        const auto authToken = authProvider_->ownerAuthInfo().value()->authToken;
        quasar::HttpClient::Headers headers = {{"Authorization", "OAuth " + authToken}};
        try {
            const auto signature = deviceCryptography_->sign(confirmDrmKeysSetParams);
            quasar::HttpClient::addSignatureHeaders(headers, signature, deviceCryptography_->getType());
        } catch (const std::exception& e) {
            YIO_LOG_ERROR_EVENT("EncryptionKeyReworker.ReportKeysSet.SignFailed", "Can't sign urlParams (signature version 2): " << e.what());
            return state;
        }
        quasar::HttpClient client("quasar-backend", device_);
        client.setTimeout(std::chrono::milliseconds{60000});
        client.setRetriesCount(0);

        try {
            const auto response = client.post("confirm-drm-keys-setting", confirmDrmKeysSetUrl, {}, headers);
            if (response.responseCode == 200) {
                resultState.status = ReworkStatus::DONE;
                return resultState;
            } else if (response.responseCode == 404) { // We don't need to set keys
                YIO_LOG_ERROR_EVENT("EncryptionKeyReworker.ReportKeysSet.404Response", "Unable to confirm setting drm keys: " << response.responseCode << ". Body: " << response.body);
                resultState.status = ReworkStatus::DONE;
                return resultState;
            } else {
                YIO_LOG_ERROR_EVENT("EncryptionKeyReworker.ReportKeysSet.Non200Response", "Unable to confirm setting drm keys: " << response.responseCode << ". Body: " << response.body);
                delayTimingsPolicy_.increaseDelayBetweenCalls();
                return resultState;
            }
        } catch (const std::exception& e) {
            YIO_LOG_INFO("Exception trying to report device keys set: " << e.what());
            delayTimingsPolicy_.increaseDelayBetweenCalls();
            return resultState;
        }
    }

    std::string statusToString(ReworkStatus status) {
        switch (status) {
            case ReworkStatus::IN_PROGRESS:
                return "In progress";
            case ReworkStatus::KEYS_RECEIVED:
                return "Keys received";
            case ReworkStatus::KEYS_BURNT:
                return "Keys burnt";
            case ReworkStatus::DONE:
                return "Rework done";
            default:
                return "Unknown status";
        }
    }

    void EncryptionKeyReworker::reworkThreadFunc() {
        ReworkState state;
        do {
            if (state.status == ReworkStatus::IN_PROGRESS && isEncryptionKeyReworkRunning_.load()) {
                state = getKeys(state);
            }
            YIO_LOG_INFO("Rework status: " << statusToString(state.status));

            if (state.status == ReworkStatus::KEYS_RECEIVED && isEncryptionKeyReworkRunning_.load()) {
                state = burnNewKeys(state);
            }
            YIO_LOG_INFO("Rework status: " << statusToString(state.status));

            if (state.status == ReworkStatus::KEYS_BURNT && isEncryptionKeyReworkRunning_.load()) {
                state = reportKeysSet(state);
            }
            YIO_LOG_INFO("Rework status: " << statusToString(state.status));

            if (state.status == ReworkStatus::DONE && isEncryptionKeyReworkRunning_.load()) {
                std::ofstream reworkProvisionedFile(reworkProvisioningMarkerFile_);
                reworkProvisionedFile.close();
                isEncryptionKeyReworkRunning_.store(false);
            }

            std::unique_lock<std::mutex> lock(mutex_);
            sendRetriesWakeupVar_.wait_for(lock, std::chrono::milliseconds(delayTimingsPolicy_.getDelayBetweenCalls()), [this]() { return !isEncryptionKeyReworkRunning_.load(); });
        } while (isEncryptionKeyReworkRunning_.load() && state.status != ReworkStatus::DONE);
    }
} // namespace yandexstation_2
