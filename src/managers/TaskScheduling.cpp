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
#include <iomanip>
#include <chrono>
#endif

#include <iostream>

using namespace MPlatform;

namespace MTaskScheduling
{
    uint32_t NUM_WORKER_THREADS;

    ALIGN(64) task_stack_t*         s_stacks;
    ALIGN(64) std::atomic<uint32_t> s_iterations[NUM_STACKS];
    std::atomic<uint64_t>           s_pri_mask_main_stack;
    std::atomic<uint64_t>           s_checkpoints[2];
    std::atomic<uint32_t>           g_quit_request;
    std::atomic<uint32_t>           g_total_executed;

#if PROFILING
    // temp storage
    struct {
        double sched_start;
        double sched_end;
        double exec_end;
        uint64_t rdtscp_ss;
        uint64_t rdtscp_se;
        uint64_t rdtscp_ee;
        uint32_t stack;
        uint64_t checkpoints_previous_frame;
        uint64_t checkpoints_current_frame;
        uint64_t reached_checkpoints;
    } prof[PROFILING_THREADS];

    // profiling log
    uint32_t profiling_i[4][PROFILING_THREADS];
    profiling_item_t profiling_log[4][PROFILING_THREADS][PROFILING_SIZE];
#endif

    void init_scheduler()
    {
        s_stacks = new task_stack_t[NUM_STACKS];

        for (uint32_t i = 0; i < NUM_STACKS; ++i)
        {
            s_stacks[i].index = i;
            s_stacks[i].tasks[0]  = { dont_do_it, (void*)(uint64_t) i, ECP_NONE, ECP_NONE };
        }

        for (uint32_t i = NUM_ACTIVE_STACKS; i < NUM_STACKS; ++i)
        {
            s_iterations[i].store(0x7FFFFFFF, std::memory_order_relaxed); // max int32 because SSE
        }

        s_pri_mask_main_stack.store((1<<NUM_ACTIVE_STACKS)-1, std::memory_order_relaxed); // all stacks allowed, main_stack 0
        s_checkpoints[0].store(0xFFFFFFFFFFFFFFFF, std::memory_order_relaxed); // none passed in frame 0
        s_checkpoints[1].store(0xFFFFFFFFFFFFFFFF, std::memory_order_relaxed); // all passed in frame -1
    }

    void clear_scheduler()
    {
        delete[] s_stacks;
    }

    void worker_thread(uint32_t thread_id)
    {
        timestamp();
        prof_sched_start(thread_id);

        uint32_t stack = 0;
        uint32_t stack_size = 0;
        uint64_t iterations_size = 0;
        uint32_t iteration = 0;

        while (!g_quit_request.load(std::memory_order_relaxed))
        {
            uint64_t pri_mask_main_stack = s_pri_mask_main_stack.load(std::memory_order_acquire);
            uint32_t main_stack = (uint32_t) (pri_mask_main_stack>>32);
            uint32_t pri_mask = (uint32_t) pri_mask_main_stack;

            task_t task;

            uint64_t c;
            // previous stack has highest priority. main stack is allways allowed to run
            uint32_t previous_stack_allowed = (uint32_t) (iteration == s_iterations[stack].load(std::memory_order_relaxed));
            uint32_t previous_stack_bit = previous_stack_allowed << (stack - main_stack) % 32;
            uint32_t main_stack_bit = 1;
            uint32_t main_stack_offset = asm_bsr32( (previous_stack_bit | main_stack_bit) & pri_mask );
            uint32_t next_pri = (main_stack_offset + main_stack) % 32;
            pri_mask &= ~(1 << main_stack_offset);
            do
            {
                stack = next_pri;

                iterations_size = s_stacks[stack].iterations_size.load(std::memory_order_acquire);
                iteration = (uint32_t) (iterations_size >> 32);
                stack_size = (uint32_t) iterations_size;
                task = s_stacks[stack].tasks[stack_size];

                // check if all required checkpoints are reached
                uint64_t current_frame = iteration;
                uint64_t previous_frame = current_frame - 1;
                uint64_t ecp_current_frame = s_checkpoints[current_frame & 1].load(std::memory_order_acquire);
                uint64_t ecp_previous_frame = s_checkpoints[previous_frame & 1].load(std::memory_order_acquire);
                c  = task.checkpoints_current_frame - ((ecp_current_frame ^ (((current_frame >> 1) & 1) - 1)) & task.checkpoints_current_frame);
                c |= task.checkpoints_previous_frame - ((ecp_previous_frame ^ (((previous_frame >> 1) & 1) - 1)) & task.checkpoints_previous_frame);
                c |= (uint64_t) stack_size == 0; // this should be handled with dont_do_it tasks

                if (c)
                {
                    if (!pri_mask)
                    {
                        // all top tasks are blocked by dependencies
                        // reload priority mask and try again (a blocking task might have finished)
                        pri_mask_main_stack = s_pri_mask_main_stack.load(std::memory_order_acquire);
                        main_stack = (uint32_t) (pri_mask_main_stack>>32);
                        pri_mask = (uint32_t) pri_mask_main_stack;
                    }
                    // task is blocked. try another stack
                    main_stack_offset = asm_bsf32(pri_mask);
                    next_pri = (main_stack_offset + main_stack) % 32;
                    pri_mask &= ~(1 << main_stack_offset);
                }

            } while ( c || !s_stacks[stack].iterations_size.compare_exchange_weak(iterations_size, iterations_size - 1, std::memory_order_acq_rel) );

            if (stack_size == 1)
            {
                // we picked the last task. update priority mask
                s_iterations[stack].fetch_add(1, std::memory_order_relaxed);

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

                    ALIGN(64) static const uint32_t range_16[16] =
                        {
                            0,  1,  2,  3,
                            4,  5,  6,  7,
                            8,  9, 10, 11,
                            12, 13, 14, 15
                        };

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

                    ALIGN(64) static const uint32_t iteration_mask[16] =
                        {
                            0x00800000, 0x00800000, 0x00800000, 0x00800000,
                            0x00008000, 0x00008000, 0x00008000, 0x00008000,
                            0x00000080, 0x00000080, 0x00000080, 0x00000080,
                            0x0F0B0703, 0x0E0A0602, 0x0D090501, 0x0C080400
                        };

                    __m128i  c = _mm_blendv_epi8(c0, c1, _mm_load_si128((__m128i*) &iteration_mask[0]));
                             c = _mm_blendv_epi8(c , c2, _mm_load_si128((__m128i*) &iteration_mask[4]));
                             c = _mm_blendv_epi8(c , c3, _mm_load_si128((__m128i*) &iteration_mask[8]));
                             c = _mm_shuffle_epi8(c, _mm_load_si128((__m128i*) &iteration_mask[12]));

                    // mask of stacks allowed to run
                    uint32_t m = _mm_movemask_epi8(c);
                    // rotate mask so that main stack is in bit 0
                             m = (m >> main_stack) | (m << (32 - main_stack));
                    // offset to new main stack (0 if old main stack is still allowed)
                    uint32_t k = asm_bsf32(m);
                    uint32_t old_main_stack = main_stack;
                    // new main stack
                    main_stack = (k + main_stack) % 32;
                    // rotate mask so that new main stack is in bit 0
                             m = (m >> k) | (m << (32 - k));
                    // stacks in range [old_main_stack, main_stack) are
                    // guaranteed to be allowed to run (starting their next frame)
                             m = m | ~( (uint32_t) ((uint64_t) 1 << (32 - (main_stack - old_main_stack) % 32)) - 1 );
                    uint32_t active_stack_mask = (1 << NUM_ACTIVE_STACKS) - 1;
                    // but make sure to zero all bits belonging to inactive stacks (32b bit-field)
                             m = m & ( (active_stack_mask >> main_stack) | (active_stack_mask << (32 - main_stack)) );

                    // pack to guarantee conformity between priority mask and main stack
                    new_pri_mask_main_stack = (uint64_t) main_stack << 32 | m;
                } while (!s_pri_mask_main_stack.compare_exchange_weak(old_pri_mask_main_stack, new_pri_mask_main_stack, std::memory_order_acq_rel));
            }

            prof_sched_end_exec_start(thread_id, stack, &task);

            uint64_t reached_checkpoints = task.execute(task.args, thread_id);

            prof_exec_end(thread_id, reached_checkpoints);
            prof_log(thread_id, iteration);

            if (g_total_executed.fetch_add(1, std::memory_order_relaxed) == 10000000)
            {
                signal_shutdown();
            }

            prof_sched_start(thread_id);

            if (reached_checkpoints) // branch to avoid unnecessary lock instruction
            {
                s_checkpoints[iteration & 1].fetch_xor(reached_checkpoints, std::memory_order_release);
            }
        }
    }

    uint64_t dont_do_it(void* args, uint32_t thread_id)
    {
        std::cout << "DONT DO IT !!!!" << std::endl;
        uint32_t s = (uint64_t)args;
        std::cout << s << " " << s_iterations[s] << std::endl;
        return ECP_NONE;
    }

    uint64_t simulate_work(uint32_t amount)
    {
        uint64_t ret = 0;

        for (uint32_t i = 0; i < amount; ++i)
        {
            ret += asm_rdtscp();
        }

        return ret;
    }


    // profiling functions
    inline void prof_sched_start(uint32_t thread_id)
    {
#if PROFILING
        prof[thread_id].sched_start = timestamp();
        prof[thread_id].rdtscp_ss = asm_rdtscp();
#endif
    }

    inline void prof_sched_end_exec_start(uint32_t thread_id, uint32_t stack, task_t* task)
    {
#if PROFILING
        prof[thread_id].rdtscp_se = asm_rdtscp();
        prof[thread_id].sched_end = timestamp();
        prof[thread_id].checkpoints_previous_frame = task->checkpoints_previous_frame;
        prof[thread_id].checkpoints_current_frame = task->checkpoints_current_frame;
        prof[thread_id].stack = stack;
#endif
    }

    inline void prof_exec_end(uint32_t thread_id, uint64_t reached_checkpoints)
    {
#if PROFILING
        prof[thread_id].exec_end = timestamp();
        prof[thread_id].rdtscp_ee = asm_rdtscp();
        prof[thread_id].reached_checkpoints = reached_checkpoints;
#endif
    }

    inline void prof_log(uint32_t thread_id, uint32_t iteration)
    {
#if PROFILING
        uint32_t it = iteration & 0x03;
        uint32_t i = profiling_i[it][thread_id];
        profiling_log[it][thread_id][i].sched_start = prof[thread_id].sched_start;
        profiling_log[it][thread_id][i].sched_end = prof[thread_id].sched_end;
        profiling_log[it][thread_id][i].exec_end = prof[thread_id].exec_end;
        profiling_log[it][thread_id][i].rdtscp_sched = prof[thread_id].rdtscp_se - prof[thread_id].rdtscp_ss;
        profiling_log[it][thread_id][i].rdtscp_exec = prof[thread_id].rdtscp_ee - prof[thread_id].rdtscp_se;
        profiling_log[it][thread_id][i].stack = prof[thread_id].stack;
        profiling_log[it][thread_id][i].checkpoints_previous_frame = prof[thread_id].checkpoints_previous_frame;
        profiling_log[it][thread_id][i].checkpoints_current_frame = prof[thread_id].checkpoints_current_frame;
        profiling_log[it][thread_id][i].reached_checkpoints = prof[thread_id].reached_checkpoints;

        ++profiling_i[it][thread_id];
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
                o << std::setprecision(9)
                  << profiling_log[0][thread][i].sched_start << " | "
                  << profiling_log[0][thread][i].sched_end << " | "
                  << profiling_log[0][thread][i].exec_end << " | "
                  << profiling_log[0][thread][i].rdtscp_sched << " | "
                  << profiling_log[0][thread][i].rdtscp_exec << " | "
                  << profiling_log[0][thread][i].stack << " | "
                  << profiling_log[0][thread][i].checkpoints_previous_frame << " | "
                  << profiling_log[0][thread][i].checkpoints_current_frame << " | "
                  << profiling_log[0][thread][i].reached_checkpoints << "\n"
                  << "\t--------------------\n";
            }
        }

        o.close();
#endif
    }

    inline double timestamp()
    {
        static auto start = std::chrono::high_resolution_clock::now();
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::micro> diff = now - start;

        return diff.count();
    }
}
