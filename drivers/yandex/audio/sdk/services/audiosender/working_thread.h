#pragma once

#include "concurrent_queue.h"

#include <yandex_io/libs/logging/logging.h>
#include <yandex_io/libs/threading/steady_condition_variable.h>

#include <memory>
#include <mutex>
#include <thread>

namespace AudioSender {

    template <class Target, class State>
    class WorkingThread final {
    public:
        using Block = std::function<void(std::shared_ptr<State>)>;
        using SharedPtr = std::shared_ptr<WorkingThread<Target, State>>;

    public:
        WorkingThread() {
            messageQueue = std::make_shared<MessageQueue>();
            workingThread = std::make_shared<std::thread>(workingThreadFunc, new WorkingThreadInput(messageQueue));
        }

        ~WorkingThread() {
            messageQueue->push(std::make_shared<Message>(Message::Quit));
            workingThread->detach();
        }

        WorkingThread(const WorkingThread&) = delete;
        WorkingThread& operator=(const WorkingThread&) = delete;

        void dispatchAsync(Block block) {
            messageQueue->push(std::make_shared<ExecMessage>(block, nullptr));
        }

        void dispatchAsync(std::weak_ptr<Target> weakTarget, Block block) {
            dispatchAsync(blockWithStrongTarget(weakTarget, block));
        }

        void dispatchSync(Block block) {
            std::unique_lock<std::mutex> lock(dropQueueMutex);

            auto syncEvent = std::make_shared<quasar::SteadyConditionVariable>();
            messageQueue->push(std::make_shared<ExecMessage>(block, syncEvent));
            syncEvent->wait(lock);
        }

        void dispatchSync(std::weak_ptr<Target> weakTarget, Block block) {
            dispatchSync(blockWithStrongTarget(weakTarget, block));
        }

        void dropQueueAndDispatchSync(Block block) {
            std::unique_lock<std::mutex> lock(dropQueueMutex);

            auto syncEvent = std::make_shared<quasar::SteadyConditionVariable>();
            messageQueue->clearAndPush(std::make_shared<ExecMessage>(block, syncEvent));
            syncEvent->wait(lock);
        }

        void dropQueueAndDispatchSync(std::weak_ptr<Target> weakTarget, Block block) {
            dropQueueAndDispatchSync(blockWithStrongTarget(weakTarget, block));
        }

    private:
        struct Message {
        public:
            enum Type {
                Exec,
                Quit
            };

            using SharedPtr = std::shared_ptr<Message>;

        public:
            Type type;

            Message(Type type)
                : type(type)
            {
            }
            virtual ~Message() {
            }
        };

        struct ExecMessage: public Message {
            Block block;
            std::shared_ptr<quasar::SteadyConditionVariable> syncEvent;

            ExecMessage(Block block, std::shared_ptr<quasar::SteadyConditionVariable> syncEvent)
                : Message(Message::Exec)
                , block(block)
                , syncEvent(syncEvent)
            {
            }
        };

        using MessageQueue = ConcurrentQueue<typename Message::SharedPtr>;

        struct WorkingThreadInput {
            typename MessageQueue::SharedPtr messageQueue;

            WorkingThreadInput(typename MessageQueue::SharedPtr messageQueue)
                : messageQueue(messageQueue)
            {
            }
        };

    private:
        static void workingThreadFunc(void* workingThreadInput) {
            YIO_DEFINE_LOG_MODULE_IN_SCOPE("audio_sender");

            std::shared_ptr<WorkingThreadInput> input(reinterpret_cast<WorkingThreadInput*>(workingThreadInput));

            try {
                std::shared_ptr<State> state = std::make_shared<State>();

                bool quitMessageReceived = false;
                while (!quitMessageReceived) {
                    typename Message::SharedPtr message = input->messageQueue->waitForNextItem();

                    switch (message->type) {
                        case Message::Exec: {
                            ExecMessage* execMessage = static_cast<ExecMessage*>(message.get());

                            try {
                                execMessage->block(state);
                            } catch (std::exception& e) {
                                YIO_LOG_ERROR_EVENT("WorkingThread.BlockStdException", e.what());
                            } catch (...) {
                                YIO_LOG_ERROR_EVENT("WorkingThread.BlockUnknownException", "");
                            }

                            if (execMessage->syncEvent != nullptr) {
                                execMessage->syncEvent->notify_one();
                            }

                            break;
                        }
                        case Message::Quit: {
                            quitMessageReceived = true;
                            break;
                        }
                        default: {
                            YIO_LOG_ERROR_EVENT("WorkingThread. Unsupported message type: ", message->type);
                            break;
                        }
                    }
                }
            } catch (const std::exception& e) {
                YIO_LOG_ERROR_EVENT("WorkingThread.GlobalStdException", e.what());
            } catch (...) {
                YIO_LOG_ERROR_EVENT("WorkingThread.GlobalUnknownException", "");
            }
        }

        static Block blockWithStrongTarget(std::weak_ptr<Target> weakTarget, Block block) {
            return {
                [weakTarget, block](std::shared_ptr<State> state) {
                    if (auto strongTarget = weakTarget.lock()) {
                        block(state);
                    }
                }};
        }

    private:
        typename MessageQueue::SharedPtr messageQueue;

        std::mutex dropQueueMutex;
        std::shared_ptr<std::thread> workingThread;
    };

} // namespace AudioSender
