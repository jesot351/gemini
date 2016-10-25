#pragma once

#include "managers/Memory.h"

#include <atomic>
#include <mutex>
#include <condition_variable>

struct GLFWwindow;

namespace SInput
{
    extern uint32_t system_id;
    extern MMemory::LinearAllocator32kb task_args_memory;
    struct input_loop_sync_t
    {
        std::mutex m;
        std::condition_variable cv;
        bool gather_input = false;
        bool input_gathered = false;
    } extern input_loop_sync;

    void init_input(uint32_t);
    void input_loop(GLFWwindow*);

    uint64_t submit_tasks(void*, uint32_t);
    uint64_t input_task(void*, uint32_t);
}
