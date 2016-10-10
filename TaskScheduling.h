#pragma once

#include "Platform.h"

#define PROFILING 1

#include <atomic>
#if PROFILING
#include <chrono>
#endif

namespace MTaskScheduling
{
    const uint32_t NUM_STACKS             = 16;
    const uint32_t NUM_ACTIVE_STACKS      = 5;
    const uint32_t STACK_SIZE             = 128;
    const uint32_t NUM_WORKER_THREADS     = MPlatform::NUM_HARDWARE_THREADS;
    const uint32_t MAX_NUM_WORKER_THREADS = 32;
    #if PROFILING
    const uint32_t PROFILING_THREADS      = 8;
    const uint32_t PROFILING_SIZE         = 256;
    #endif

    enum scheduling_checkpoint_t : uint64_t
    {
        SCP_NONE       = 0,
        SCP_INPUT1     = ((uint64_t)1<<0),
        SCP_PHYSICS1   = ((uint64_t)1<<1),
        SCP_PHYSICS2   = ((uint64_t)1<<2),
        SCP_PHYSICS3   = ((uint64_t)1<<3),
        SCP_PHYSICS4   = ((uint64_t)1<<4),
        SCP_ANIMATION1 = ((uint64_t)1<<5),
        SCP_ANIMATION2 = ((uint64_t)1<<6),
        SCP_ANIMATION3 = ((uint64_t)1<<7),
        SCP_AI1        = ((uint64_t)1<<8),
        SCP_AI2        = ((uint64_t)1<<9),
        SCP_STREAMING1 = ((uint64_t)1<<10),
        SCP_STREAMING2 = ((uint64_t)1<<11),
        SCP_STREAMING3 = ((uint64_t)1<<12),
        SCP_STREAMING4 = ((uint64_t)1<<13),
        SCP_SOUND1     = ((uint64_t)1<<14),
        SCP_RENDERING1 = ((uint64_t)1<<15),
        SCP_RENDERING2 = ((uint64_t)1<<16),
        SCP_RENDERING3 = ((uint64_t)1<<17),
        SCP_NEVER      = ((uint64_t)1<<63),
    };



#if PROFILING
    typedef struct
    {
        double sched_start;
        double sched_end;
        double exec_end;
        uint64_t rdtscp_sched;
        uint64_t rdtscp_exec;
        uint32_t stack;
        uint64_t checkpoints_previous_frame;
        uint64_t checkpoints_current_frame;
        uint64_t reached_checkpoints;
    } profiling_item_t;
#endif

    typedef struct
    {
        uint64_t (*execute)(void*, uint32_t);
        void* args;
        uint64_t checkpoints_previous_frame;
        uint64_t checkpoints_current_frame;
    } ALIGN(16) task_t;


    extern ALIGN(64) task_t                s_stacks[NUM_STACKS][STACK_SIZE];
    extern ALIGN(64) std::atomic<uint32_t> s_stack_sizes[NUM_STACKS];
    extern ALIGN(64) std::atomic<uint32_t> s_iterations[NUM_STACKS];
    extern std::atomic<uint64_t>           s_pri_mask_main_stack;
    extern std::atomic<uint64_t>           s_checkpoints[2];

    extern std::atomic<uint32_t>           g_quit_request;
    extern std::atomic<uint32_t>           g_total_executed;
    extern uint32_t                        g_frame;

#if PROFILING
    extern uint32_t profiling_i[PROFILING_THREADS];
    extern profiling_item_t profiling_log[PROFILING_THREADS][PROFILING_SIZE];
#endif


    void init_scheduler();
    void worker_thread(uint32_t);
    uint64_t dont_do_it(void*, uint32_t);
#if PROFILING
    double timepoint_to_double(std::chrono::time_point<std::chrono::high_resolution_clock>);
    void   write_profiling();
#endif
}
