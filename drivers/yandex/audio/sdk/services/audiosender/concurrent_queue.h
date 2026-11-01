#pragma once

#include <yandex_io/libs/threading/steady_condition_variable.h>

#include <mutex>
#include <queue>

namespace AudioSender {

    template <class Item>
    class ConcurrentQueue {
    public:
        using SharedPtr = std::shared_ptr<ConcurrentQueue>;

        void clear() {
            std::queue<Item> dummy;
            {
                std::lock_guard<std::mutex> lock(mutex);
                dummy.swap(queue);
            }
        }

        void push(Item item) {
            std::lock_guard<std::mutex> lock(mutex);
            queue.push(item);
            queueNotEmptyCondition.notify_one();
        }

        Item waitForNextItem() {
            std::unique_lock<std::mutex> lock(mutex);
            queueNotEmptyCondition.wait(lock, [this] { return !queue.empty(); });
            Item item = queue.front();
            queue.pop();
            return item;
        }

    private:
        std::mutex mutex;
        std::queue<Item> queue;
        quasar::SteadyConditionVariable queueNotEmptyCondition;
    };

} // namespace AudioSender
