#include "TaskScheduling.h"

#include <iostream>
#include <thread>

int main()
{
    MTaskScheduling::init_scheduler();

    for (uint32_t active_stack = 0; active_stack < MTaskScheduling::NUM_ACTIVE_STACKS; ++active_stack)
    {
        MTaskScheduling::dummy_fill_stack((void*)(uint64_t) active_stack, 0);
    }

    uint32_t num_threads = MTaskScheduling::NUM_WORKER_THREADS;
    std::cout << "num threads: " << num_threads << "\n";

    std::thread workers[num_threads];
    for (uint32_t i = 0; i < num_threads; ++i)
    {
        workers[i] = std::thread(MTaskScheduling::worker_thread, i);
    }

    for (uint32_t i = 0; i < num_threads; ++i)
    {
        workers[i].join();
    }

    #if PROFILING
    MTaskScheduling::write_profiling();
    #endif

    std::cout << "total executed: " << MTaskScheduling::g_total_executed.load(std::memory_order_relaxed) << "\n";

    return 0;
}
