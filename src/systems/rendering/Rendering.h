#pragma once

#include "managers/Memory.h"

#include <atomic>

struct GLFWwindow;

namespace SRendering
{
    extern uint32_t system_id;
    extern MMemory::LinearAllocator32kb task_args_memory;

    void init_rendering(uint32_t, GLFWwindow*);
    void clear_rendering();

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

    typedef struct
    {
        uint32_t thread_log;
        uint32_t iteration;
        std::atomic<double>* start_time;
        void* buffer_memory;
        std::atomic<uint32_t>* write_offset;
        std::atomic<uint32_t>* counter;
    } write_perf_overlay_task_args_t;
    uint64_t write_perf_overlay_task(void* args, uint32_t thread_id);

    uint64_t present_task(void*, uint32_t);
}
