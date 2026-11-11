#include "BoundedTaskRunner.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

int main()
{
    constexpr std::size_t taskCount = 1000;
    constexpr std::size_t maxWorkers = 3;
    std::atomic_size_t activeWorkers{ 0 };
    std::atomic_size_t maximumActiveWorkers{ 0 };
    std::vector<unsigned int> executions(taskCount, 0);

    BoundedTaskRunner::ForEach(taskCount, maxWorkers, [&](std::size_t taskIndex) {
        const std::size_t active = activeWorkers.fetch_add(1, std::memory_order_relaxed) + 1;
        std::size_t observedMaximum = maximumActiveWorkers.load(std::memory_order_relaxed);
        while (observedMaximum < active &&
            !maximumActiveWorkers.compare_exchange_weak(
                observedMaximum, active, std::memory_order_relaxed))
        {
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        ++executions[taskIndex];
        activeWorkers.fetch_sub(1, std::memory_order_relaxed);
    });

    if (maximumActiveWorkers.load() > maxWorkers)
    {
        std::cerr << "The worker limit was exceeded.\n";
        return 1;
    }
    for (const auto executionCount : executions)
    {
        if (executionCount != 1)
        {
            std::cerr << "A task was skipped or executed more than once.\n";
            return 1;
        }
    }

    std::atomic_size_t completedTasks{ 0 };
    bool exceptionPropagated = false;
    try
    {
        BoundedTaskRunner::ForEach(100, maxWorkers, [&](std::size_t taskIndex) {
            if (taskIndex == 23)
            {
                throw std::runtime_error("expected task failure");
            }
            completedTasks.fetch_add(1, std::memory_order_relaxed);
        });
    }
    catch (const std::runtime_error&)
    {
        exceptionPropagated = true;
    }
    if (!exceptionPropagated || completedTasks.load() != 99)
    {
        std::cerr << "Task exceptions were not rethrown after all tasks completed.\n";
        return 1;
    }

    return 0;
}
