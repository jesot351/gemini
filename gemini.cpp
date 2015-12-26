#include <iostream>
#include <fstream>
#include <atomic>
#include <thread>
#include <chrono>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <emmintrin.h>
#include <tmmintrin.h> // _mm_shuffle_epi8
#include <smmintrin.h> // _mm_blendv_epi8

#define NUM_STACKS 16
#define NUM_ACTIVE_STACKS 8
#define STACK_SIZE 128
#define NUM_THREADS 4

#define ALIGN(x) __attribute__((aligned(x)))

#ifndef _MM_SHUFFLE
#define _MM_SHUFFLE(z, y, x, w) (z<<6) | (y<<4) | (x<<2) | w
#endif

#define DEBUG 1
#if DEBUG
#define DEBUG_SIZE 256
#endif

typedef struct 
{
    void (*execute)(void*, uint32_t);
    void* args;
    uint32_t required_lock;
    uint32_t exclusive_execution;
} ALIGN(16) task_t;

inline uint32_t asm_bsf(uint32_t m)
{
    uint32_t i;
    __asm__ ("bsfl %1, %0"
            :"=r" (i)
            :"r" (m)
            :"cc");
    return i;
}

inline uint32_t asm_bsr(uint32_t m)
{
    uint32_t i;
    __asm__ ("bsrl %1, %0"
            :"=r" (i)
            :"r" (m)
            :"cc");
    return i;
}

inline uint64_t asm_rdtscp(void)
{
    uint64_t tsc;
    __asm__ __volatile__(
        "rdtscp;"
        "shl $32, %%rdx;"
        "or %%rdx, %%rax"
        : "=a"(tsc)
        :
        : "%rcx", "%rdx");
    return tsc;
}

#if DEBUG
struct debug_item
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
};

uint32_t debug_i[NUM_THREADS];
debug_item debug_log[NUM_THREADS][DEBUG_SIZE];

#endif
std::atomic<uint32_t> total_executed;


ALIGN(64) const uint32_t range_16[16]       = {          0,          1,          2,          3, 
                                                         4,          5,          6,          7, 
                                                         8,          9,         10,         11, 
                                                        12,         13,         14,         15 };

ALIGN(64) const uint32_t iteration_mask[16] = { 0x00800000, 0x00800000, 0x00800000, 0x00800000, 
                                                0x00008000, 0x00008000, 0x00008000, 0x00008000, 
                                                0x00000080, 0x00000080, 0x00000080, 0x00000080, 
                                                0x0F0B0703, 0x0E0A0602, 0x0D090501, 0x0C080400 };


void   worker_thread(uint32_t);
void   work(void*, uint32_t);
void   fill_stack(void*, uint32_t);
void   dont_do_it(void*, uint32_t);
double timepoint_to_double(std::chrono::time_point<std::chrono::high_resolution_clock>);
void   write_debug();


std::atomic<uint32_t> g_quit_request;
uint32_t g_frame;

task_t s_stacks[NUM_STACKS][STACK_SIZE];
ALIGN(64) std::atomic<uint32_t> s_stack_sizes[NUM_STACKS];
ALIGN(64) std::atomic<uint32_t> s_stack_locks[NUM_STACKS];
ALIGN(64) std::atomic<uint32_t> s_stack_running[NUM_STACKS];
ALIGN(64) uint32_t s_iterations[NUM_STACKS];
std::atomic<uint32_t> s_pri_mask;
std::atomic<uint32_t> s_main_stack;


int main()
{
    s_stacks[0][0]  = { dont_do_it, NULL, 15, 1 };
    s_stacks[1][0]  = { dont_do_it, NULL, 15, 1 };
    s_stacks[2][0]  = { dont_do_it, NULL, 15, 1 };
    s_stacks[3][0]  = { dont_do_it, NULL, 15, 1 };
    s_stacks[4][0]  = { dont_do_it, NULL, 15, 1 };
    s_stacks[5][0]  = { dont_do_it, NULL, 15, 1 };
    s_stacks[6][0]  = { dont_do_it, NULL, 15, 1 };
    s_stacks[7][0]  = { dont_do_it, NULL, 15, 1 };
    s_stacks[8][0]  = { dont_do_it, NULL, 15, 1 };
    s_stacks[9][0]  = { dont_do_it, NULL, 15, 1 };
    s_stacks[10][0] = { dont_do_it, NULL, 15, 1 };
    s_stacks[11][0] = { dont_do_it, NULL, 15, 1 };
    s_stacks[12][0] = { dont_do_it, NULL, 15, 1 };
    s_stacks[13][0] = { dont_do_it, NULL, 15, 1 };
    s_stacks[14][0] = { dont_do_it, NULL, 15, 1 };
    s_stacks[15][0] = { dont_do_it, NULL, 15, 1 };

    fill_stack((void*) 0, 0);
    fill_stack((void*) 1, 0);
    fill_stack((void*) 2, 0);
    fill_stack((void*) 3, 0);
    fill_stack((void*) 4, 0);
    fill_stack((void*) 5, 0);
    fill_stack((void*) 6, 0);
    fill_stack((void*) 7, 0);

    s_iterations[8]  = 0x7FFFFFFF;
    s_iterations[9]  = 0x7FFFFFFF;
    s_iterations[10] = 0x7FFFFFFF;
    s_iterations[11] = 0x7FFFFFFF;
    s_iterations[12] = 0x7FFFFFFF;
    s_iterations[13] = 0x7FFFFFFF;
    s_iterations[14] = 0x7FFFFFFF;
    s_iterations[15] = 0x7FFFFFFF;

    s_stack_locks[ 8].store(1, std::memory_order_relaxed);
    s_stack_locks[ 9].store(1, std::memory_order_relaxed);
    s_stack_locks[10].store(1, std::memory_order_relaxed);
    s_stack_locks[11].store(1, std::memory_order_relaxed);
    s_stack_locks[12].store(1, std::memory_order_relaxed);
    s_stack_locks[13].store(1, std::memory_order_relaxed);
    s_stack_locks[14].store(1, std::memory_order_relaxed);
    s_stack_locks[15].store(1, std::memory_order_relaxed);

    s_pri_mask.store((1<<NUM_ACTIVE_STACKS)-1, std::memory_order_relaxed);

    std::thread workers[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; ++i)
    {
        workers[i] = std::thread(worker_thread, i);
    }

    for (int i = 0; i < NUM_THREADS; ++i)
    {
        workers[i].join();
    }

    #if DEBUG
    write_debug();

    #endif
    std::cout << "total executed: " << total_executed.load(std::memory_order_relaxed) << "\n";

    return 0;
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
        #if DEBUG
        debug_log[thread_id][debug_i[thread_id]].sched_start = timepoint_to_double(std::chrono::high_resolution_clock::now());
        uint64_t rdtscp_ss = asm_rdtscp();
        #endif

        uint32_t main_stack = s_main_stack.load(std::memory_order_acquire);
        uint32_t m = s_pri_mask.load(std::memory_order_relaxed);

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
                k = asm_bsf(m);
                next_pri = (k + main_stack) % 32;
                m &= ~(1<<k);
            }

        } while ( !(c && s_stack_sizes[s].compare_exchange_weak(ss, ss - 1, std::memory_order_acq_rel)) );

        // is this thread safe enough?
        if (s_stack_sizes[s].load(std::memory_order_relaxed) == 0)
        {
             main_stack = s_main_stack.load(std::memory_order_relaxed);

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

            s_pri_mask.store(m, std::memory_order_relaxed);
            s_main_stack.store(main_stack, std::memory_order_release);
        }

        #if DEBUG
        uint64_t rdtscp_se = asm_rdtscp();
        debug_log[thread_id][debug_i[thread_id]].rdtscp_sched = rdtscp_se - rdtscp_ss;
        debug_log[thread_id][debug_i[thread_id]].sched_end = timepoint_to_double(std::chrono::high_resolution_clock::now());
        debug_log[thread_id][debug_i[thread_id]].exclusive_execution = task.exclusive_execution;
        debug_log[thread_id][debug_i[thread_id]].required_lock = task.required_lock;
        uint64_t rdtscp_es = asm_rdtscp();
        #endif

        task.execute((void*)(uint64_t) s, thread_id);

        #if DEBUG
        uint64_t rdtscp_ee = asm_rdtscp();
        debug_log[thread_id][debug_i[thread_id]].rdtscp_exec = rdtscp_ee - rdtscp_es;
        debug_log[thread_id][debug_i[thread_id]].exec_end = timepoint_to_double(std::chrono::high_resolution_clock::now());
        ++debug_i[thread_id];
        #endif

        if (total_executed.fetch_add(1, std::memory_order_relaxed) == 100 * 8)
        {
            g_quit_request.store(1, std::memory_order_relaxed);
        }
    }
}

void work(void* args, uint32_t thread_id)
{
    uint64_t stack_id = (uint64_t) args;
    
    #if DEBUG
    debug_log[thread_id][debug_i[thread_id]].stack = (uint32_t) stack_id;

    for (int i = 0; i < NUM_STACKS; ++i)
    {
        debug_log[thread_id][debug_i[thread_id]].sizes[i]   = s_stack_sizes[i].load(std::memory_order_relaxed);
        debug_log[thread_id][debug_i[thread_id]].locks[i]   = s_stack_locks[i].load(std::memory_order_relaxed);
        debug_log[thread_id][debug_i[thread_id]].running[i] = s_stack_running[i].load(std::memory_order_relaxed);
    }
    #endif

    srand(time(0));
    usleep(100);
}

void fill_stack(void* args, uint32_t thread_id)
{
    uint32_t stack_id = (uint64_t) args;

    #if DEBUG
    debug_log[thread_id][debug_i[thread_id]].stack = stack_id;
    #endif

    s_stacks[stack_id][1] = { fill_stack, args, 15, 1 };

    srand(time(0));
    for (int i = 2; i < 101; ++i)
    {
        s_stacks[stack_id][i] = { work, (void*)(uint64_t) i, (rand() % 60 == 0 ? stack_id + 1 : 15), (rand() % 30 == 0) };
    }

    s_stack_sizes[stack_id].store(100, std::memory_order_release);
}

void dont_do_it(void* args, uint32_t thread_id)
{
    static int count = 0;
    ++count;
    std::cout << count << " t_id: " << thread_id << " args: " << (uint64_t) args << "\n";
}

#if DEBUG
double timepoint_to_double(std::chrono::time_point<std::chrono::high_resolution_clock> now)
{
    static auto start = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> diff = now - start;

    return diff.count();
}
#endif

#if DEBUG
void write_debug()
{
    std::ofstream o;
    o.open("debug/debug.txt");

    for (int thread = 0; thread < NUM_THREADS; ++thread)
    {
        o << "THREAD " << thread << ":\n";

        for (int i = 0; i < DEBUG_SIZE; ++i)
        {
            double ss = debug_log[thread][i].sched_start;
            double se = debug_log[thread][i].sched_end;
            double ee = debug_log[thread][i].exec_end;
            o << ss << " | " << se << " | " << ee << " | " << debug_log[thread][i].rdtscp_sched << " | " << debug_log[thread][i].rdtscp_exec << " | ";
            o << debug_log[thread][i].stack << " | " << debug_log[thread][i].exclusive_execution << " | " << debug_log[thread][i].required_lock << "\n";
            o << "\t--------------------\n";
        }
    }

    o.close();
}
#endif