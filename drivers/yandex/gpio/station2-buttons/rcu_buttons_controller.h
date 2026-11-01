#pragma once

#include <yandex_io/sdk/sdk_state.h>
#include <yandex_io/sdk/sdk_state_observer.h>
#include <yandex_io/sdk/sdk_interface.h>

#include <memory>
#include <mutex>

class RcuButtonsController final: public YandexIO::SDKStateObserver {
public:
    explicit RcuButtonsController(std::shared_ptr<YandexIO::SDKInterface> sdk);
    ~RcuButtonsController() override = default;

    void onSDKState(const YandexIO::SDKState& state) override;

    bool handleNavRightPress();
    bool handleNavLeftPress();
    bool handleMainPress();
    bool handleBackPress();

private:
    const std::shared_ptr<YandexIO::SDKInterface> sdk_;
    std::mutex mutex_;
    YandexIO::SDKState deviceState_;
};
