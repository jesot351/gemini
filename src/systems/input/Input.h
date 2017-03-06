#pragma once

#include "managers/TaskScheduling.h"
#include "managers/Memory.h"

#include <atomic>
#include <mutex>
#include <condition_variable>

#include <GLFW/glfw3.h>

namespace SInput
{
    extern MTaskScheduling::task_stack_t* task_stack;
    extern MMemory::LinearAllocator32kb task_args_memory;
    struct input_loop_sync_t
    {
        std::mutex m;
        std::condition_variable cv;
        bool gather_input = false;
        bool input_gathered = false;
    } extern input_loop_sync;

    void init_input(MTaskScheduling::task_stack_t*);
    void input_loop(GLFWwindow*);
    void key_callback(GLFWwindow*, int, int, int, int);

    uint64_t submit_tasks(void*, uint32_t);
    uint64_t input_task(void*, uint32_t);
}
