#pragma once

#include "Platform.h"

#define PROFILING 0

#include <atomic>
#if PROFILING
#include <chrono>
#endif

namespace MTaskScheduling
{
    const uint32_t NUM_STACKS         = 16;
    const uint32_t NUM_ACTIVE_STACKS  = 8;
    const uint32_t STACK_SIZE         = 128;
    const uint32_t NUM_WORKER_THREADS = MPlatform::NUM_HARDWARE_THREADS;
    const uint32_t TASK_NO_LOCK       = NUM_STACKS - 1;
    #if PROFILING
    const uint32_t PROFILING_THREADS  = 8;
    const uint32_t PROFILING_SIZE     = 256;
    #endif


    #if PROFILING
    typedef struct
    {
        double sched_start;
        double sched_end;
        double exec_end;
        uint64_t rdtscp_sched;
        uint64_t rdtscp_exec;
        uint32_t stack;
        uint32_t exclusive_execution;
        uint32_t required_lock;
        uint32_t sizes[NUM_STACKS];
        uint32_t locks[NUM_STACKS];
        uint32_t running[NUM_STACKS];
    } profiling_item_t;
    #endif

    typedef struct 
    {
        void (*execute)(void*, uint32_t);
        void* args;
        uint32_t required_lock;
        uint32_t exclusive_execution;
    } ALIGN(16) task_t;


    extern ALIGN(64) task_t                s_stacks[NUM_STACKS][STACK_SIZE];
    extern ALIGN(64) std::atomic<uint32_t> s_stack_sizes[NUM_STACKS];
    extern ALIGN(64) std::atomic<uint32_t> s_stack_locks[NUM_STACKS];
    extern ALIGN(64) std::atomic<uint32_t> s_stack_running[NUM_STACKS];
    extern ALIGN(64) uint32_t              s_iterations[NUM_STACKS];
    extern std::atomic<uint64_t>           s_pri_mask_main_stack;

    extern std::atomic<uint32_t>           g_quit_request;
    extern std::atomic<uint32_t>           g_total_executed;
    extern uint32_t                        g_frame;

    #if PROFILING
    extern uint32_t profiling_i[PROFILING_THREADS];
    extern profiling_item_t profiling_log[PROFILING_THREADS][PROFILING_SIZE];
    #endif


    void   init_scheduler();
    void   worker_thread(uint32_t);
    void   dont_do_it(void*, uint32_t);
    #if PROFILING
    double timepoint_to_double(std::chrono::time_point<std::chrono::high_resolution_clock>);
    void   write_profiling();
    #endif

    void   dummy_work(void*, uint32_t);
    void   dummy_fill_stack(void*, uint32_t);
}