#pragma once

#include "app/base/foreground_app.h"
#include "foreground_app_animation.h"

#include <yandex_io/protos/model_objects.pb.h>
#include <yandex_io/sdk/directive_observer.h>
#include <yandex_io/libs/signals/signal.h>
#include <yandex_io/libs/threading/periodic_executor.h>

#include <memory>

class ForegroundActivity: public YandexIO::DirectiveObserver {
public:
    using IsActiveSignal = quasar::Signal<quasar::ISignal<bool>>;

    ForegroundActivity(
        std::weak_ptr<YandexStationFrontalController> ledDevice,
        std::shared_ptr<ForegroundApp> app);

    [[nodiscard]] bool isActive() const;

    [[nodiscard]] std::shared_ptr<IsActiveSignal> isActiveSignal() const;

    [[nodiscard]] std::shared_ptr<ForegroundAppAnimation> getAnimation() const;

    [[nodiscard]] bool handleQuasarMessage(const quasar::ipc::SharedMessage& message);

    void onDirective(const std::string& name, const std::string& vinsRequestId, const std::string& jsonPayload) override;

private:
    void setIsActive(bool isActive);

    void updateLastActionTimestamp();

    void disableIfNeed();

private:
    std::shared_ptr<ForegroundApp> app_;
    std::shared_ptr<ForegroundAppAnimation> animation_;

    bool isActive_;
    std::shared_ptr<IsActiveSignal> isActiveSignal_;

    std::mutex mutex_;

    std::chrono::steady_clock::time_point lastActionTimestamp_;
    std::unique_ptr<quasar::PeriodicExecutor> disableIfNeedChecker_;
};
