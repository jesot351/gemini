#pragma once

#include "managers/Platform.h"

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
    extern uint32_t NUM_WORKER_THREADS;//     = MPlatform::NUM_HARDWARE_THREADS;
    const uint32_t MAX_NUM_WORKER_THREADS = 32;
#if PROFILING
    const uint32_t PROFILING_THREADS      = 8;
    const uint32_t PROFILING_SIZE         = 256;
#endif

    enum execution_checkpoint_t : uint64_t
    {
        ECP_NONE                         = 0,
        ECP_INPUT1                       = ((uint64_t)1<<0),
        ECP_PHYSICS1                     = ((uint64_t)1<<1),
        ECP_PHYSICS2                     = ((uint64_t)1<<2),
        ECP_PHYSICS3                     = ((uint64_t)1<<3),
        ECP_PHYSICS4                     = ((uint64_t)1<<4),
        ECP_ANIMATION1                   = ((uint64_t)1<<5),
        ECP_ANIMATION2                   = ((uint64_t)1<<6),
        ECP_ANIMATION3                   = ((uint64_t)1<<7),
        ECP_AI1                          = ((uint64_t)1<<8),
        ECP_AI2                          = ((uint64_t)1<<9),
        ECP_STREAMING1                   = ((uint64_t)1<<10),
        ECP_STREAMING2                   = ((uint64_t)1<<11),
        ECP_STREAMING3                   = ((uint64_t)1<<12),
        ECP_STREAMING4                   = ((uint64_t)1<<13),
        ECP_SOUND1                       = ((uint64_t)1<<14),
        ECP_RENDERING1                   = ((uint64_t)1<<15),
        ECP_RENDERING2                   = ((uint64_t)1<<16),
        ECP_RENDERING3                   = ((uint64_t)1<<17),
        ECP_RENDERING_WRITE_PERF_OVERLAY = ((uint64_t)1<<18),
        ECP_RENDERING_PRESENT            = ((uint64_t)1<<19),
    };

    typedef struct task_t
    {
        uint64_t (*execute)(void*, uint32_t);
        void* args;
        uint64_t checkpoints_previous_frame;
        uint64_t checkpoints_current_frame;
    } task_t;

    typedef struct task_stack_t
    {
        uint32_t index;
        uint32_t unpublished_size;
        std::atomic<uint64_t> iterations_size; // pack to guarantee conformity
        ALIGN(32) task_t tasks[STACK_SIZE];
    } ALIGN(64) task_stack_t;

    extern ALIGN(64) task_stack_t*         s_stacks;
    extern ALIGN(64) std::atomic<uint32_t> s_iterations[NUM_STACKS];
    extern std::atomic<uint32_t>           g_quit_request;
    extern std::atomic<uint32_t>           g_total_executed;

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

    extern uint32_t profiling_i[4][PROFILING_THREADS];
    extern profiling_item_t profiling_log[4][PROFILING_THREADS][PROFILING_SIZE];
#endif


    void init_scheduler();
    void clear_scheduler();
    void worker_thread(uint32_t);
    uint64_t dont_do_it(void*, uint32_t);
    uint64_t simulate_work(uint32_t amount = 10e3);

    inline void begin_task_recording(task_stack_t* stack)
    {
        stack->unpublished_size = 1;
    };

    inline void record_task(task_stack_t* stack, task_t task)
    {
        stack->tasks[stack->unpublished_size] = task;
        ++stack->unpublished_size;
    }

    inline void submit_task_recording(task_stack_t* stack)
    {
        // pack to guarantee conformity between num stack iterations and stack size
        uint64_t iterations_size = (uint64_t) s_iterations[stack->index] << 32 | (stack->unpublished_size - 1);
        stack->iterations_size.store(iterations_size, std::memory_order_release);
    }

    // profiling functions
    void prof_sched_start(uint32_t);
    void prof_sched_end_exec_start(uint32_t, uint32_t, task_t*);
    void prof_exec_end(uint32_t, uint64_t);
    void prof_log(uint32_t, uint32_t);
    void write_profiling();
    double timestamp();
}
