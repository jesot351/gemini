#include "systems/animation/Animation.h"
#include "managers/TaskScheduling.h"
#include "managers/Memory.h"

#include <unistd.h> // usleep()
#include <iostream>

namespace SAnimation
{
    uint32_t system_id;
    MMemory::LinearAllocator32kb task_args_memory;

    void init_animation(uint32_t assigned_system_id)
    {
        system_id = assigned_system_id;
        task_args_memory.Init();
        submit_tasks(nullptr, 0);
    }

    std::atomic<uint32_t> num_executed_group1;
    std::atomic<uint32_t> num_executed_group2;
    std::atomic<uint32_t> num_executed_group3;

    uint64_t submit_tasks(void* args, uint32_t thread_id)
    {
        task_args_memory.Clear();

        uint32_t stack_size = 1;
        MTaskScheduling::task_t task;
        task.execute = submit_tasks;
        task.args = nullptr;
        task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
        task.checkpoints_current_frame = MTaskScheduling::SCP_ANIMATION3;
        MTaskScheduling::s_stacks[system_id][stack_size] = task;

        // 10 tasks in task group 3
        num_executed_group3.store(9, std::memory_order_relaxed);
        for (uint32_t i = 0; i < 10; ++i)
        {
            ++stack_size;
            MTaskScheduling::task_t task;
            task.execute = task_group3;
            task_group3_args_t* args = new(task_args_memory) task_group3_args_t;
            args->counter = &num_executed_group3;
            task.args = args;
            task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
            task.checkpoints_current_frame = MTaskScheduling::SCP_ANIMATION2;
            MTaskScheduling::s_stacks[system_id][stack_size] = task;
        }

        // 4 independent tasks
        for (uint32_t i = 0; i < 4; ++i)
        {
            ++stack_size;
            MTaskScheduling::task_t task;
            task.execute = independent_task;
            independent_task_args_t* args = new(task_args_memory) independent_task_args_t;
            args->some_param = 42 - i;
            task.args = args;
            task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
            task.checkpoints_current_frame = MTaskScheduling::SCP_NONE;
            MTaskScheduling::s_stacks[system_id][stack_size] = task;
        }

        // 10 tasks in task group 2
        num_executed_group2.store(9, std::memory_order_relaxed);
        for (uint32_t i = 0; i < 10; ++i)
        {
            ++stack_size;
            MTaskScheduling::task_t task;
            task.execute = task_group2;
            task_group2_args_t* args = new(task_args_memory) task_group2_args_t;
            args->counter = &num_executed_group2;
            task.args = args;
            task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
            task.checkpoints_current_frame = MTaskScheduling::SCP_INPUT1 | MTaskScheduling::SCP_ANIMATION1;
            MTaskScheduling::s_stacks[system_id][stack_size] = task;
        }

        // 4 independent tasks
        for (uint32_t i = 0; i < 4; ++i)
        {
            ++stack_size;
            MTaskScheduling::task_t task;
            task.execute = independent_task;
            independent_task_args_t* args = new(task_args_memory) independent_task_args_t;
            args->some_param = 42 + i;
            task.args = args;
            task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
            task.checkpoints_current_frame = MTaskScheduling::SCP_NONE;
            MTaskScheduling::s_stacks[system_id][stack_size] = task;
        }

        // 10 tasks in task group 1
        num_executed_group1.store(9, std::memory_order_relaxed);
        for (uint32_t i = 0; i < 10; ++i)
        {
            ++stack_size;
            MTaskScheduling::task_t task;
            task.execute = task_group1;
            task_group1_args_t* args = new(task_args_memory) task_group1_args_t;
            args->counter = &num_executed_group1;
            task.args = args;
            task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
            task.checkpoints_current_frame = MTaskScheduling::SCP_NONE;
            MTaskScheduling::s_stacks[system_id][stack_size] = task;
        }

        MTaskScheduling::s_stack_sizes[system_id].store((MTaskScheduling::s_iterations[system_id] << 7) | stack_size, std::memory_order_release);

        return MTaskScheduling::SCP_NONE;
    }

    uint64_t independent_task(void* args, uint32_t thread_id)
    {
        MTaskScheduling::simulate_work();

        return MTaskScheduling::SCP_NONE;
    }

    uint64_t task_group1(void* args, uint32_t thread_id)
    {
        task_group1_args_t* pargs = (task_group1_args_t*) args;

        MTaskScheduling::simulate_work();

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = MTaskScheduling::SCP_NONE;
        if (count == 0)
            reached_checkpoints = MTaskScheduling::SCP_ANIMATION1;

        return reached_checkpoints;
    }

    uint64_t task_group2(void* args, uint32_t thread_id)
    {
        task_group2_args_t* pargs = (task_group2_args_t*) args;

        MTaskScheduling::simulate_work();

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = MTaskScheduling::SCP_NONE;
        if (count == 0)
            reached_checkpoints = MTaskScheduling::SCP_ANIMATION2;

        return reached_checkpoints;
    }

    uint64_t task_group3(void* args, uint32_t thread_id)
    {
        task_group3_args_t* pargs = (task_group3_args_t*) args;

        MTaskScheduling::simulate_work();

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = MTaskScheduling::SCP_NONE;
        if (count == 0)
            reached_checkpoints = MTaskScheduling::SCP_ANIMATION3;

        return reached_checkpoints;
    }
}
