#pragma once

#include <algorithm>
#include <atomic>
#include <exception>
#include <stdexcept>
#include <thread>
#include <vector>

namespace BoundedTaskRunner
{
    // The task may be called concurrently and must support simultaneous invocations.
    template <typename Task>
    void ForEach(std::size_t taskCount, std::size_t maxWorkers, Task&& task)
    {
        if (taskCount == 0)
        {
            return;
        }
        if (maxWorkers == 0)
        {
            throw std::invalid_argument("maxWorkers must be greater than zero.");
        }

        const std::size_t workerCount = std::min(taskCount, maxWorkers);
        std::atomic_size_t nextIndex{ 0 };
        std::vector<std::exception_ptr> workerErrors(taskCount);
        std::vector<std::jthread> workers;
        workers.reserve(workerCount);

        for (std::size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex)
        {
            workers.emplace_back([&]() {
                for (;;)
                {
                    const std::size_t taskIndex = nextIndex.fetch_add(1, std::memory_order_relaxed);
                    if (taskIndex >= taskCount)
                    {
                        return;
                    }

                    try
                    {
                        task(taskIndex);
                    }
                    catch (...)
                    {
                        workerErrors[taskIndex] = std::current_exception();
                    }
                }
            });
        }

        for (auto& worker : workers)
        {
            worker.join();
        }
        for (const auto& workerError : workerErrors)
        {
            if (workerError)
            {
                std::rethrow_exception(workerError);
            }
        }
    }
}
