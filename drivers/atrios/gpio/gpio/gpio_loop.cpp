#include "gpio_loop.h"

#include <yandex_io/libs/errno/errno_exception.h>
#include <yandex_io/libs/logging/logging.h>

#include <cerrno>
#include <iostream>

#include <poll.h>
#include <unistd.h>
#include <sys/timerfd.h>

using namespace quasar;

GPIOLoop::GPIOLoop(std::optional<std::chrono::milliseconds> maxPollTimeout)
    : maxPollTimeout_(maxPollTimeout)
{
}

void GPIOLoop::addInput(GPIOInput* input)
{
    inputs_.push_back(input);
    for (const GPIO* gpio : input->getHandles()) {
        handleToInput_[gpio->getHandle()] = input;
        handleToGpio_[gpio->getHandle()] = gpio;
    }
}

void GPIOLoop::start(std::atomic_bool& thread_exists, const int wakeUpFd)
{
    while (thread_exists)
    {
        std::vector<struct pollfd> pollfds;
        std::optional<std::chrono::milliseconds> pollTimeout = maxPollTimeout_;
        for (auto handleInput : handleToGpio_)
        {
            struct pollfd arg;
            arg.fd = handleInput.second->getHandle();
            arg.events = POLLPRI | POLLERR;
            arg.revents = 0;
            pollfds.push_back(arg);

            if ((handleInput.second->read() == 0 && handleInput.second->getEdge() == GPIO::Edge::FALLING) ||
                (handleInput.second->read() == 1 && handleInput.second->getEdge() == GPIO::Edge::RISING)) {
                pollTimeout = std::chrono::milliseconds(50);
            }
        }
        /* Add wake up fd last */
        pollfds.push_back({wakeUpFd, POLLPRI | POLLERR | POLLIN, 0});

        int err = poll(pollfds.data(), pollfds.size(), pollTimeout ? pollTimeout->count() : -1);
        if (err < 0)
        {
            throw ErrnoException(errno, "poll failed");
        }

        if (wakeUpFd >= 0 && pollfds[pollfds.size() - 1].revents != 0) {
            /* Got event from wake Up fd. It means that ButtonsEndpoint is being destroyed.
             * Break buttons polling
             */
            break;
        }

        if (0 == err)
        {
            checkInputs();
            continue;
        }

        try {
            for (auto pfd = pollfds.begin(); pfd != pollfds.end() - 1; ++pfd)
            {
                if ((pfd->revents & POLLPRI) != 0)
                {
                    handleToInput_[pfd->fd]->processEvent();
                }
            }
        } catch (std::exception& e)
        {
            checkInputs();
        }
    }
}

void GPIOLoop::checkInputs()
{
    bool reloaded = false;
    for (auto& handleInput : handleToInput_)
    {
        try {
            handleInput.second->processEvent();
        } catch (std::exception& e)
        {
            std::cerr << "Reloading" << std::endl;
            handleInput.second->reload();
            reloaded = true;
        }
    }
    if (reloaded)
    {
        handleToInput_.clear();
        for (GPIOInput* input : inputs_)
        {
            for (const GPIO* gpio : input->getHandles())
            {
                handleToInput_[gpio->getHandle()] = input;
            }
        }
    }
}
