#include "systems/Input.h"
#include "managers/TaskScheduling.h"
#include "managers/Memory.h"

#include <unistd.h> // usleep()
#include <iostream>

namespace SInput
{
    uint32_t system_id;
    MMemory::LinearAllocator32kb task_args_memory;

    void init_input(uint32_t assigned_system_id)
    {
        system_id = assigned_system_id;
        task_args_memory.Init();
        submit_tasks(nullptr, 0);
    }

    uint64_t submit_tasks(void* args, uint32_t thread_id)
    {
        #if PROFILING
        MTaskScheduling::profiling_log[thread_id][MTaskScheduling::profiling_i[thread_id] % MTaskScheduling::PROFILING_SIZE].stack = system_id;
        #endif

        task_args_memory.Clear();

        MTaskScheduling::task_t task;

        task.execute = submit_tasks;
        task.args = nullptr;
        task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
        task.checkpoints_current_frame = MTaskScheduling::SCP_INPUT1;
        uint32_t stack_size = 1;
        MTaskScheduling::s_stacks[system_id][stack_size] = task;

        task.execute = input_task;
        task.args = nullptr;
        task.checkpoints_previous_frame = MTaskScheduling::SCP_RENDERING1;
        task.checkpoints_current_frame = MTaskScheduling::SCP_NONE;
        ++stack_size;
        MTaskScheduling::s_stacks[system_id][stack_size] = task;

        MTaskScheduling::s_stack_sizes[system_id].store((MTaskScheduling::s_iterations[system_id] << 7) | stack_size, std::memory_order_release);

        return MTaskScheduling::SCP_NONE;
    }

    uint64_t input_task(void* args, uint32_t thread_id)
    {
        #if PROFILING
        MTaskScheduling::profiling_log[thread_id][MTaskScheduling::profiling_i[thread_id] % MTaskScheduling::PROFILING_SIZE].stack = system_id;
        #endif

        usleep(100);

        return MTaskScheduling::SCP_INPUT1;
    }
}
