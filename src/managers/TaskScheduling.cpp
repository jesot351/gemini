#include "gemini.h"
#include "managers/TaskScheduling.h"
#include "managers/Platform.h"

#include <atomic>
#include <cassert>
#include <unistd.h>    // usleep
#include <emmintrin.h> // SSE2
#include <tmmintrin.h> // SSSE3: _mm_shuffle_epi8
#include <smmintrin.h> // SSE4.1: _mm_blendv_epi8
#if PROFILING
#include <fstream>
#include <chrono>
#endif

#include <iostream>

using namespace MPlatform;

namespace MTaskScheduling
{

    ALIGN(64) task_t                s_stacks[NUM_STACKS][STACK_SIZE];
    ALIGN(64) std::atomic<uint32_t> s_stack_sizes[NUM_STACKS];
    ALIGN(64) std::atomic<uint32_t> s_iterations[NUM_STACKS];
    std::atomic<uint64_t>           s_pri_mask_main_stack;
    std::atomic<uint64_t>           s_checkpoints[2];
    std::atomic<uint32_t>           g_quit_request;
    std::atomic<uint32_t>           g_total_executed;
    uint32_t                        g_frame;

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

    uint32_t profiling_i[PROFILING_THREADS];
    profiling_item_t profiling_log[PROFILING_THREADS][PROFILING_SIZE];
    uint64_t rdtscp_ss[PROFILING_THREADS];
    uint64_t rdtscp_es[PROFILING_THREADS];
#endif

    ALIGN(64) const uint32_t range_16[16]       = {          0,          1,          2,          3,
                                                             4,          5,          6,          7,
                                                             8,          9,         10,         11,
                                                            12,         13,         14,         15 };

    ALIGN(64) const uint32_t iteration_mask[16] = { 0x00800000, 0x00800000, 0x00800000, 0x00800000,
                                                    0x00008000, 0x00008000, 0x00008000, 0x00008000,
                                                    0x00000080, 0x00000080, 0x00000080, 0x00000080,
                                                    0x0F0B0703, 0x0E0A0602, 0x0D090501, 0x0C080400 };

    void init_scheduler()
    {
        for (uint32_t stack = 0; stack < NUM_STACKS; ++stack)
        {
            s_stacks[stack][0]  = { dont_do_it, (void*)(uint64_t) stack, SCP_NONE, SCP_NONE };
        }

        for (uint32_t inactive_stack = NUM_ACTIVE_STACKS; inactive_stack < NUM_STACKS; ++inactive_stack)
        {
            s_iterations[inactive_stack]  = 0x7FFFFFFF; // max int32 because SSE
        }

        s_pri_mask_main_stack.store((1<<NUM_ACTIVE_STACKS)-1, std::memory_order_relaxed); // all stacks allowed, main_stack 0
        s_checkpoints[0].store(0xFFFFFFFFFFFFFFFF, std::memory_order_relaxed); // none passed in frame 0
        s_checkpoints[1].store(0xFFFFFFFFFFFFFFFF, std::memory_order_relaxed); // all passed in frame -1
    }

    void worker_thread(uint32_t thread_id)
    {
        timepoint_to_double(std::chrono::high_resolution_clock::now());
        prof_sched_start(thread_id);

        uint32_t s = 0;
        uint32_t ss = 0;

        while (!g_quit_request.load(std::memory_order_relaxed))
        {
            uint64_t pri_mask_main_stack = s_pri_mask_main_stack.load(std::memory_order_acquire);
            uint32_t main_stack = (uint32_t) (pri_mask_main_stack>>32);
            uint32_t m = (uint32_t) pri_mask_main_stack;

            task_t task;

            uint64_t c;
            uint32_t k = asm_bsr32( ( ((uint32_t) ((ss >> 7) == s_iterations[s].load(std::memory_order_relaxed)) << (s - main_stack) % 32) | 1 ) & m);
            uint32_t next_pri = (k + main_stack) % 32;
            m &= ~(1 << k);
            do
            {
                s = next_pri;

                ss = s_stack_sizes[s].load(std::memory_order_acquire);
                task = s_stacks[s][ss & 0x0000007F];

                uint64_t current_frame = ss >> 7;
                uint64_t previous_frame = current_frame - 1;
                uint64_t scp_current_frame = s_checkpoints[current_frame & 1].load(std::memory_order_acquire);
                uint64_t scp_previous_frame = s_checkpoints[previous_frame & 1].load(std::memory_order_acquire);
                c  = task.checkpoints_current_frame - ((scp_current_frame ^ (((current_frame >> 1) & 1) - 1)) & task.checkpoints_current_frame);
                c |= task.checkpoints_previous_frame - ((scp_previous_frame ^ (((previous_frame >> 1) & 1) - 1)) & task.checkpoints_previous_frame);
                c |= (uint64_t) (ss & 0x0000007F) == 0; // this should be handled with dont_do_it tasks

                if (c)
                {
                    if (!m) // only happens when we run out of ready tasks
                    {
                        pri_mask_main_stack = s_pri_mask_main_stack.load(std::memory_order_acquire);
                        m = (uint32_t) pri_mask_main_stack;
                        main_stack = (uint32_t) (pri_mask_main_stack>>32);
                    }
                    k = asm_bsf32(m);
                    next_pri = (k + main_stack) % 32;
                    m &= ~(1 << k);
                }

            } while ( c || !s_stack_sizes[s].compare_exchange_weak(ss, ss - 1, std::memory_order_acq_rel) );

            if ((ss & 0x0000007F) == 1)
            {
                s_iterations[s].fetch_add(1, std::memory_order_relaxed);

                uint64_t old_pri_mask_main_stack = s_pri_mask_main_stack.load(std::memory_order_relaxed);
                uint64_t new_pri_mask_main_stack = 0;
                do
                {
                    main_stack = (uint32_t) (old_pri_mask_main_stack >> 32);

                    __m128i ms = _mm_set1_epi32(main_stack);

                    __m128i i0 = _mm_load_si128((__m128i*) &s_iterations[0]);
                    __m128i i1 = _mm_load_si128((__m128i*) &s_iterations[4]);
                    __m128i i2 = _mm_load_si128((__m128i*) &s_iterations[8]);
                    __m128i i3 = _mm_load_si128((__m128i*) &s_iterations[12]);

                            i0 = _mm_add_epi32(i0, _mm_cmpgt_epi32(ms, _mm_load_si128((__m128i*) &range_16[0])));
                            i1 = _mm_add_epi32(i1, _mm_cmpgt_epi32(ms, _mm_load_si128((__m128i*) &range_16[4])));
                            i2 = _mm_add_epi32(i2, _mm_cmpgt_epi32(ms, _mm_load_si128((__m128i*) &range_16[8])));
                            i3 = _mm_add_epi32(i3, _mm_cmpgt_epi32(ms, _mm_load_si128((__m128i*) &range_16[12])));

                    __m128i l0 = _mm_min_epi32(i0, i1);
                            l0 = _mm_min_epi32(l0, i2);
                            l0 = _mm_min_epi32(l0, i3);
                    __m128i l1 = _mm_shuffle_epi32(l0, 0x1B);
                            l0 = _mm_min_epi32(l0, l1);
                            l1 = _mm_shuffle_epi32(l0, 0x01);
                            l0 = _mm_min_epi32(l0, l1);
                            l0 = _mm_shuffle_epi32(l0, 0x00);

                    __m128i c0 = _mm_cmpeq_epi32(i0, l0);
                    __m128i c1 = _mm_cmpeq_epi32(i1, l0);
                    __m128i c2 = _mm_cmpeq_epi32(i2, l0);
                    __m128i c3 = _mm_cmpeq_epi32(i3, l0);

                    __m128i  c = _mm_blendv_epi8(c0, c1, _mm_load_si128((__m128i*) &iteration_mask[0]));
                             c = _mm_blendv_epi8(c , c2, _mm_load_si128((__m128i*) &iteration_mask[4]));
                             c = _mm_blendv_epi8(c , c3, _mm_load_si128((__m128i*) &iteration_mask[8]));
                             c = _mm_shuffle_epi8(c, _mm_load_si128((__m128i*) &iteration_mask[12]));

                    uint32_t m = _mm_movemask_epi8(c);
                             m = (m >> main_stack) | (m << (32 - main_stack));
                             k = asm_bsf32(m);
                    uint32_t old_main_stack = main_stack;
                    main_stack = (k + main_stack) % 32;
                             m = (m >> k) | (m << (32 - k));
                             m = m | ~( (uint32_t) ((uint64_t) 1 << (32 - (main_stack - old_main_stack) % 32)) - 1 );
                    uint32_t active_stack_mask = (1 << NUM_ACTIVE_STACKS) - 1;
                             m = m & ( (active_stack_mask >> main_stack) | (active_stack_mask << (32 - main_stack)) );

                    // pack to guarantee conformity between pri mask and main stack
                    new_pri_mask_main_stack = (uint64_t) main_stack << 32 | m;
                } while (!s_pri_mask_main_stack.compare_exchange_weak(old_pri_mask_main_stack, new_pri_mask_main_stack, std::memory_order_acq_rel));
            }

            prof_sched_end_exec_start(thread_id, s, &task);

            uint64_t reached_checkpoints = task.execute(task.args, thread_id);

            prof_exec_end(thread_id);

            if (g_total_executed.fetch_add(1, std::memory_order_relaxed) == 10000000)
            {
                signal_shutdown();
            }

            prof_sched_start(thread_id);

            if (reached_checkpoints) // branch to avoid unnecessary lock instruction
            {
                s_checkpoints[(ss >> 7) & 1].fetch_xor(reached_checkpoints, std::memory_order_release);
            }
        }
    }

    uint64_t dont_do_it(void* args, uint32_t thread_id)
    {
        std::cout << "DONT DO IT !!!!" << std::endl;
        uint32_t s = (uint64_t)args;
        std::cout << s << " " << s_iterations[s] << std::endl;
        return SCP_NONE;
    }


    // profiling functions
    void prof_sched_start(uint32_t thread_id)
    {
#if PROFILING
        uint32_t i = profiling_i[thread_id] % PROFILING_SIZE;
        profiling_log[thread_id][i].sched_start = timepoint_to_double(std::chrono::high_resolution_clock::now());
        rdtscp_ss[thread_id] = asm_rdtscp();
#endif
    }

    void prof_sched_end_exec_start(uint32_t thread_id, uint32_t stack, task_t* task)
    {
#if PROFILING
        uint64_t rdtscp_se = asm_rdtscp();
        uint32_t i = profiling_i[thread_id] % PROFILING_SIZE;
        profiling_log[thread_id][i].rdtscp_sched = rdtscp_se - rdtscp_ss[thread_id];
        profiling_log[thread_id][i].sched_end = timepoint_to_double(std::chrono::high_resolution_clock::now());
        profiling_log[thread_id][i].checkpoints_previous_frame = task->checkpoints_previous_frame;
        profiling_log[thread_id][i].checkpoints_current_frame = task->checkpoints_current_frame;
        profiling_log[thread_id][i].stack = stack;
        rdtscp_es[thread_id] = asm_rdtscp();
#endif
    }

    void prof_exec_end(uint32_t thread_id)
    {
#if PROFILING
        uint64_t rdtscp_ee = asm_rdtscp();
        uint32_t i = profiling_i[thread_id] % PROFILING_SIZE;
        profiling_log[thread_id][i].rdtscp_exec = rdtscp_ee - rdtscp_es[thread_id];
        profiling_log[thread_id][i].exec_end = timepoint_to_double(std::chrono::high_resolution_clock::now());
        ++profiling_i[thread_id];
#endif
    }

    void write_profiling()
    {
#if PROFILING
        std::ofstream o;
        o.open("debug/debug.txt");

        for (uint32_t thread = 0; thread < NUM_WORKER_THREADS; ++thread)
        {
            o << "THREAD " << thread << ":\n";

            for (uint32_t i = 0; i < PROFILING_SIZE; ++i)
            {
                o << profiling_log[thread][i].sched_start << " | "
                  << profiling_log[thread][i].sched_end << " | "
                  << profiling_log[thread][i].exec_end << " | "
                  << profiling_log[thread][i].rdtscp_sched << " | "
                  << profiling_log[thread][i].rdtscp_exec << " | "
                  << profiling_log[thread][i].stack << " | "
                  << profiling_log[thread][i].checkpoints_previous_frame << " | "
                  << profiling_log[thread][i].checkpoints_current_frame << "\n"
                  << "\t--------------------\n";
            }
        }

        o.close();
#endif
    }

    double timepoint_to_double(std::chrono::time_point<std::chrono::high_resolution_clock> now)
    {
        static auto start = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> diff = now - start;

        return diff.count();
    }
}
