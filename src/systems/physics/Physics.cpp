#include "systems/physics/Physics.h"
#include "managers/TaskScheduling.h"
#include "managers/Memory.h"

#include <unistd.h> // usleep()
#include <iostream>

using namespace MTaskScheduling;

namespace SPhysics
{
    task_stack_t* task_stack;
    MMemory::LinearAllocator32kb task_args_memory;

    void init_physics(task_stack_t* assigned_task_stack)
    {
        task_stack = assigned_task_stack;
        task_args_memory.Init();
        submit_tasks(nullptr, 0);
    }

    std::atomic<uint32_t> num_executed_group1;
    std::atomic<uint32_t> num_executed_group2;
    std::atomic<uint32_t> num_executed_group3;
    std::atomic<uint32_t> num_executed_group4;

    uint64_t submit_tasks(void* args, uint32_t thread_id)
    {
        task_args_memory.Clear();

        begin_task_recording(task_stack);

        record_task(task_stack, {submit_tasks, nullptr, ECP_NONE, ECP_PHYSICS4});

        // 10 tasks in task group 4
        num_executed_group4.store(9, std::memory_order_relaxed);
        for (uint32_t i = 0; i < 10; ++i)
        {
            task_group4_args_t* args = new(task_args_memory) task_group4_args_t;
            args->counter = &num_executed_group4;

            record_task(task_stack, {task_group4, args, ECP_NONE, ECP_PHYSICS3});
        }

        // 4 independent tasks
        for (uint32_t i = 0; i < 4; ++i)
        {
            independent_task_args_t* args = new(task_args_memory) independent_task_args_t;
            args->some_param = 42 - i;

            record_task(task_stack, {independent_task, args, ECP_NONE, ECP_NONE});
        }

        // 10 tasks in task group 3
        num_executed_group3.store(9, std::memory_order_relaxed);
        for (uint32_t i = 0; i < 10; ++i)
        {
            task_group3_args_t* args = new(task_args_memory) task_group3_args_t;
            args->counter = &num_executed_group3;

            record_task(task_stack, {task_group3, args, ECP_RENDERING2, ECP_PHYSICS2});
        }

        // 4 independent tasks
        for (uint32_t i = 0; i < 4; ++i)
        {
            independent_task_args_t* args = new(task_args_memory) independent_task_args_t;
            args->some_param = 42 - i;

            record_task(task_stack, {independent_task, args, ECP_NONE, ECP_NONE});
        }

        // 10 tasks in task group 2
        num_executed_group2.store(9, std::memory_order_relaxed);
        for (uint32_t i = 0; i < 10; ++i)
        {
            task_group2_args_t* args = new(task_args_memory) task_group2_args_t;
            args->counter = &num_executed_group2;

            record_task(task_stack, {task_group2, args, ECP_NONE, ECP_PHYSICS1});
        }

        // 4 independent tasks
        for (uint32_t i = 0; i < 4; ++i)
        {
            independent_task_args_t* args = new(task_args_memory) independent_task_args_t;
            args->some_param = 42 + i;

            record_task(task_stack, {independent_task, args, ECP_NONE, ECP_NONE});
        }

        // 10 tasks in task group 1
        num_executed_group1.store(9, std::memory_order_relaxed);
        for (uint32_t i = 0; i < 10; ++i)
        {
            task_group1_args_t* args = new(task_args_memory) task_group1_args_t;
            args->counter = &num_executed_group1;

            record_task(task_stack, {task_group1, args, ECP_NONE, ECP_INPUT1});
        }

        // 4 independent tasks
        for (uint32_t i = 0; i < 4; ++i)
        {
            independent_task_args_t* args = new(task_args_memory) independent_task_args_t;
            args->some_param = 42 + i;

            record_task(task_stack, {independent_task, args, ECP_NONE, ECP_NONE});
        }

        submit_task_recording(task_stack);

        return ECP_NONE;
    }

    uint64_t independent_task(void* args, uint32_t thread_id)
    {
        simulate_work();

        return ECP_NONE;
    }

    uint64_t task_group1(void* args, uint32_t thread_id)
    {
        task_group1_args_t* pargs = (task_group1_args_t*) args;

        simulate_work();

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = ECP_NONE;
        if (count == 0)
            reached_checkpoints = ECP_PHYSICS1;

        return reached_checkpoints;
    }

    uint64_t task_group2(void* args, uint32_t thread_id)
    {
        task_group2_args_t* pargs = (task_group2_args_t*) args;

        simulate_work();

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = ECP_NONE;
        if (count == 0)
            reached_checkpoints = ECP_PHYSICS2;

        return reached_checkpoints;
    }

    uint64_t task_group3(void* args, uint32_t thread_id)
    {
        task_group3_args_t* pargs = (task_group3_args_t*) args;

        simulate_work();

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = ECP_NONE;
        if (count == 0)
            reached_checkpoints = ECP_PHYSICS3;

        return reached_checkpoints;
    }

    uint64_t task_group4(void* args, uint32_t thread_id)
    {
        task_group4_args_t* pargs = (task_group4_args_t*) args;

        simulate_work();

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = ECP_NONE;
        if (count == 0)
            reached_checkpoints = ECP_PHYSICS4;

        return reached_checkpoints;
    }
}
