#include "TaskScheduling.h"
#include "Platform.h"

#include <atomic>
#include <unistd.h>    // usleep
#include <emmintrin.h> // SSE2
#include <tmmintrin.h> // SSSE3: _mm_shuffle_epi8
#include <smmintrin.h> // SSE4.1: _mm_blendv_epi8
#if PROFILING
#include <fstream>
#include <chrono>
#endif

using namespace MPlatform;

namespace MTaskScheduling
{

    ALIGN(64) task_t                s_stacks[NUM_STACKS][STACK_SIZE];
    ALIGN(64) std::atomic<uint32_t> s_stack_sizes[NUM_STACKS];
    ALIGN(64) std::atomic<uint32_t> s_stack_locks[NUM_STACKS];
    ALIGN(64) std::atomic<uint32_t> s_stack_running[NUM_STACKS];
    ALIGN(64) uint32_t              s_iterations[NUM_STACKS];
    std::atomic<uint64_t>           s_pri_mask_main_stack;

    std::atomic<uint32_t>           g_quit_request;
    std::atomic<uint32_t>           g_total_executed;
    uint32_t                        g_frame;

    #if PROFILING
    uint32_t profiling_i[PROFILING_THREADS];
    profiling_item_t profiling_log[PROFILING_THREADS][PROFILING_SIZE];
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
            s_stacks[stack][0]  = { dont_do_it, nullptr, TASK_NO_LOCK, 1 };
        }

        for (uint32_t inactive_stack = NUM_ACTIVE_STACKS; inactive_stack < NUM_STACKS; ++inactive_stack)
        {
            s_iterations[inactive_stack]  = 0x7FFFFFFF;
            s_stack_locks[inactive_stack].store(1, std::memory_order_relaxed);
        }

        s_pri_mask_main_stack.store((1<<NUM_ACTIVE_STACKS)-1, std::memory_order_relaxed);
    }

    void worker_thread(uint32_t thread_id)
    {
        uint32_t sl0 = 15;
        uint32_t l0  =  0;
        uint32_t s   =  0;
        uint32_t sl  = 15;

        s_stack_locks[sl].fetch_add(1, std::memory_order_relaxed);
        s_stack_running[s].fetch_add(1, std::memory_order_relaxed);

        while (!g_quit_request.load(std::memory_order_relaxed))
        {
            #if PROFILING
            profiling_log[thread_id][profiling_i[thread_id]].sched_start = timepoint_to_double(std::chrono::high_resolution_clock::now());
            uint64_t rdtscp_ss = asm_rdtscp();
            #endif

            uint64_t pri_mask_main_stack = s_pri_mask_main_stack.load(std::memory_order_acquire);
            uint32_t main_stack = (uint32_t) (pri_mask_main_stack>>32);
            uint32_t m = (uint32_t) pri_mask_main_stack;

            s_stack_locks[sl0].fetch_sub(l0, std::memory_order_relaxed);
            uint32_t ss0 = s_stack_sizes[main_stack].load(std::memory_order_acquire);
                     sl0 = s_stacks[main_stack][ss0].required_lock;
                     l0  = (uint32_t)(s_stack_locks[main_stack].load(std::memory_order_relaxed) == 0) &
                           ( (uint32_t)(s_stack_running[main_stack].load(std::memory_order_relaxed) == 0) | (uint32_t)(s_stacks[main_stack][ss0].exclusive_execution == 0) ) &
                           (uint32_t)(ss0 > 0);
            s_stack_locks[sl0].fetch_add(l0, std::memory_order_relaxed);

            task_t task;

            uint32_t c;
            uint32_t ss;
            uint32_t k = asm_bsr(((1<<((s - main_stack) % 32)) | 1) & m);
            uint32_t next_pri = (k + main_stack) % 32;
            m &= ~(1<<k);
            do
            {
                s_stack_locks[sl].fetch_sub(1, std::memory_order_relaxed);
                s_stack_running[s].fetch_sub(1, std::memory_order_relaxed);

                s = next_pri;

                ss = s_stack_sizes[s].load(std::memory_order_acquire);
                task = s_stacks[s][ss];
                sl = task.required_lock;
                s_stack_locks[sl].fetch_add(1, std::memory_order_relaxed);
                s_stack_running[s].fetch_add(1, std::memory_order_relaxed);

                std::atomic_thread_fence(std::memory_order_seq_cst);    // bleh

                c = (uint32_t)(s_stack_locks[s].load(std::memory_order_relaxed) == 0) &
                    ( (uint32_t)(s_stack_running[s].load(std::memory_order_relaxed) == 1) | (uint32_t)(task.exclusive_execution == 0) ) &
                    (uint32_t)(s_stack_running[sl].load(std::memory_order_relaxed) == 0) &
                    (uint32_t)(ss > 0);

                if (!c)
                {
                    if (!m) // only happens when we run out of ready tasks
                    {
                        pri_mask_main_stack = s_pri_mask_main_stack.load(std::memory_order_acquire);
                        m = (uint32_t) pri_mask_main_stack;
                        main_stack = (uint32_t) (pri_mask_main_stack>>32);
                    }
                    k = asm_bsf(m);
                    next_pri = (k + main_stack) % 32;
                    m &= ~(1<<k);
                }


            } while ( !(c && s_stack_sizes[s].compare_exchange_weak(ss, ss - 1, std::memory_order_acq_rel)) );

            // is this thread safe enough?
            if (s_stack_sizes[s].load(std::memory_order_relaxed) == 0)
            {
                main_stack = (uint32_t) (s_pri_mask_main_stack.load(std::memory_order_relaxed)>>32);

                __m128i  i = _mm_set1_epi32(s_iterations[main_stack] + 1);
                __m128i ms = _mm_set1_epi32(main_stack);
                
                ++s_iterations[s];

                __m128i i0 = _mm_load_si128((__m128i*) &s_iterations[0]);
                __m128i i1 = _mm_load_si128((__m128i*) &s_iterations[4]);
                __m128i i2 = _mm_load_si128((__m128i*) &s_iterations[8]);
                __m128i i3 = _mm_load_si128((__m128i*) &s_iterations[12]);

                /*// ---- frame count
                __m128i l0 = _mm_min_epi32(i0, i1);
                        l0 = _mm_min_epi32(l0, i2);
                        l0 = _mm_min_epi32(l0, i3);
                __m128i l1 = _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(l0), _mm_castsi128_ps(l0), _MM_SHUFFLE(0, 0, 3, 2)));
                        l0 = _mm_min_epi32(l0, l1);
                        l1 = _mm_castps_si128(_mm_shuffle_ps(_mm_castsi128_ps(l0), _mm_castsi128_ps(l0), _MM_SHUFFLE(0, 0, 0, 1)));
                        l0 = _mm_min_epi32(l0, l1);
                uint32_t ua[4];
                _mm_store_si128((__m128i*) &ua[0], l0);
                   g_frame = ua[0];
                // ---- end frame count*/

                        i0 = _mm_add_epi32(i0, _mm_cmpgt_epi32(ms, _mm_load_si128((__m128i*) &range_16[0])));
                        i1 = _mm_add_epi32(i1, _mm_cmpgt_epi32(ms, _mm_load_si128((__m128i*) &range_16[4])));
                        i2 = _mm_add_epi32(i2, _mm_cmpgt_epi32(ms, _mm_load_si128((__m128i*) &range_16[8])));
                        i3 = _mm_add_epi32(i3, _mm_cmpgt_epi32(ms, _mm_load_si128((__m128i*) &range_16[12])));

                __m128i c0 = _mm_cmplt_epi32(i0, i);
                __m128i c1 = _mm_cmplt_epi32(i1, i);
                __m128i c2 = _mm_cmplt_epi32(i2, i);
                __m128i c3 = _mm_cmplt_epi32(i3, i);

                __m128i  c = _mm_blendv_epi8(c0, c1, _mm_load_si128((__m128i*) &iteration_mask[0]));
                         c = _mm_blendv_epi8(c , c2, _mm_load_si128((__m128i*) &iteration_mask[4]));
                         c = _mm_blendv_epi8(c , c3, _mm_load_si128((__m128i*) &iteration_mask[8]));
                         c = _mm_shuffle_epi8(c, _mm_load_si128((__m128i*) &iteration_mask[12]));

                uint32_t m = _mm_movemask_epi8(c);
                         m = (m >> main_stack) | (m << (32 - main_stack));
                         k = asm_bsf(m);
                main_stack = (k + main_stack) % 32;
                         m = (m >> k) | (m << (32 - k));

                // pack to guarantee conformity between pri mask and main stack
                s_pri_mask_main_stack.store(((uint64_t) main_stack)<<32 | m, std::memory_order_release);
            }

            #if PROFILING
            uint64_t rdtscp_se = asm_rdtscp();
            profiling_log[thread_id][profiling_i[thread_id]].rdtscp_sched = rdtscp_se - rdtscp_ss;
            profiling_log[thread_id][profiling_i[thread_id]].sched_end = timepoint_to_double(std::chrono::high_resolution_clock::now());
            profiling_log[thread_id][profiling_i[thread_id]].exclusive_execution = task.exclusive_execution;
            profiling_log[thread_id][profiling_i[thread_id]].required_lock = task.required_lock;
            uint64_t rdtscp_es = asm_rdtscp();
            #endif

            task.execute((void*)(uint64_t) s, thread_id);

            #if PROFILING
            uint64_t rdtscp_ee = asm_rdtscp();
            profiling_log[thread_id][profiling_i[thread_id]].rdtscp_exec = rdtscp_ee - rdtscp_es;
            profiling_log[thread_id][profiling_i[thread_id]].exec_end = timepoint_to_double(std::chrono::high_resolution_clock::now());
            ++profiling_i[thread_id];
            #endif

            if (g_total_executed.fetch_add(1, std::memory_order_relaxed) == 1000)
            {
                g_quit_request.store(1, std::memory_order_relaxed);
            }
        }
    }

    void dont_do_it(void* args, uint32_t thread_id)
    {
        
    }

    #if PROFILING
    double timepoint_to_double(std::chrono::time_point<std::chrono::high_resolution_clock> now)
    {
        static auto start = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> diff = now - start;

        return diff.count();
    }
    #endif

    #if PROFILING
    void write_profiling()
    {
        std::ofstream o;
        o.open("debug/debug.txt");

        for (uint32_t thread = 0; thread < NUM_WORKER_THREADS; ++thread)
        {
            o << "THREAD " << thread << ":\n";

            for (uint32_t i = 0; i < PROFILING_SIZE; ++i)
            {
                double ss = profiling_log[thread][i].sched_start;
                double se = profiling_log[thread][i].sched_end;
                double ee = profiling_log[thread][i].exec_end;
                o << ss << " | " << se << " | " << ee << " | " << profiling_log[thread][i].rdtscp_sched << " | " << profiling_log[thread][i].rdtscp_exec << " | ";
                o << profiling_log[thread][i].stack << " | " << profiling_log[thread][i].exclusive_execution << " | " << profiling_log[thread][i].required_lock << "\n";
                o << "\t--------------------\n";
            }
        }

        o.close();
    }
    #endif


    void dummy_work(void* args, uint32_t thread_id)
    {
        #if PROFILING
        uint32_t stack_id = (uint64_t) args;
        profiling_log[thread_id][profiling_i[thread_id]].stack = stack_id;

        for (uint32_t i = 0; i < NUM_STACKS; ++i)
        {
            profiling_log[thread_id][profiling_i[thread_id]].sizes[i]   = s_stack_sizes[i].load(std::memory_order_relaxed);
            profiling_log[thread_id][profiling_i[thread_id]].locks[i]   = s_stack_locks[i].load(std::memory_order_relaxed);
            profiling_log[thread_id][profiling_i[thread_id]].running[i] = s_stack_running[i].load(std::memory_order_relaxed);
        }
        #endif

        usleep(100);
    }

    void dummy_fill_stack(void* args, uint32_t thread_id)
    {
        uint32_t stack_id = (uint64_t) args;

        #if PROFILING
        profiling_log[thread_id][profiling_i[thread_id]].stack = stack_id;
        #endif

        s_stacks[stack_id][1] = { dummy_fill_stack, args, TASK_NO_LOCK, 1 };

        srand(time(0));
        for (uint32_t i = 2; i < 101; ++i)
        {
            s_stacks[stack_id][i] = { dummy_work, (void*)(uint64_t) i, (rand() % 60 == 0 ? stack_id + 1 : 15), (rand() % 30 == 0) };
        }

        s_stack_sizes[stack_id].store(100, std::memory_order_release);
    }
}