#pragma once

#include "Memory.h"

#include <atomic>

namespace SAnimation
{
    extern uint32_t system_id;
    extern MMemory::LinearAllocator32kb task_args_memory;

    void init_animation(uint32_t);

    uint64_t submit_tasks(void*, uint32_t);

    typedef struct
    {
        uint32_t some_param;
    } independent_task_args_t;
    uint64_t independent_task(void*, uint32_t);

    typedef struct
    {
        std::atomic<uint32_t>* counter;
    } task_group1_args_t;
    uint64_t task_group1(void*, uint32_t);

    typedef struct
    {
        std::atomic<uint32_t>* counter;
    } task_group2_args_t;
    uint64_t task_group2(void*, uint32_t);

    typedef struct
    {
        std::atomic<uint32_t>* counter;
    } task_group3_args_t;
    uint64_t task_group3(void*, uint32_t);
}
