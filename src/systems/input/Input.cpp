#include "gemini.h"
#include "systems/input/Input.h"
#include "managers/TaskScheduling.h"
#include "managers/Memory.h"
#include "managers/Platform.h"
#include "data/Input.h"

#include <iostream>
#include <mutex>
#include <condition_variable>

#include <GLFW/glfw3.h>

using namespace MTaskScheduling;

namespace SInput
{
    task_stack_t* task_stack;
    MMemory::LinearAllocator32kb task_args_memory;
    input_loop_sync_t input_loop_sync;

    ALIGN(16) uint32_t key_events[NUM_KEY_STATES][8];
    uint32_t num_events[NUM_KEY_STATES];
    const uint32_t max_num_events[NUM_KEY_STATES] = {8, 8, 1, 8};

    void init_input(task_stack_t* assigned_task_stack)
    {
        task_stack = assigned_task_stack;
        task_args_memory.Init();
        submit_tasks(nullptr, 0);
    }

    void input_loop(GLFWwindow* window)
    {
        glfwSetKeyCallback(window, key_callback);

        while (!MTaskScheduling::g_quit_request.load(std::memory_order_relaxed))
        {
            // wait for notification
            std::unique_lock<std::mutex> l(input_loop_sync.m);
            input_loop_sync.cv.wait(l, []{ return input_loop_sync.gather_input; });
            input_loop_sync.gather_input = false;

            // clear input structures
            __m128i zero = _mm_setzero_si128(); // perhaps the compiler is better at this
            _mm_store_si128((__m128i*) &key_events[0][0], zero);
            _mm_store_si128((__m128i*) &key_events[0][4], zero);
            _mm_store_si128((__m128i*) &key_events[1][0], zero);
            _mm_store_si128((__m128i*) &key_events[1][4], zero);
            _mm_store_si128((__m128i*) &key_events[2][0], zero);
            _mm_store_si128((__m128i*) &key_events[2][4], zero);
            num_events[0] = 0;
            num_events[1] = 0;
            num_events[2] = 0;

            glfwPollEvents();

            if (glfwWindowShouldClose(window))
            {
                l.unlock();
                signal_shutdown();
                return;
            }

            // notify completion
            input_loop_sync.input_gathered = true;
            l.unlock();
            input_loop_sync.cv.notify_one();
        }
    }

    void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        key_events[action][num_events[action]] = key;
        num_events[action] = (num_events[action] + 1) & (max_num_events[action] - 1);
    }

    uint64_t submit_tasks(void* args, uint32_t thread_id)
    {
        task_args_memory.Clear();

        begin_task_recording(task_stack);

        record_task(task_stack, {submit_tasks, nullptr, ECP_NONE, ECP_INPUT1});
        record_task(task_stack, {input_task, nullptr, ECP_RENDERING_PRESENT, ECP_NONE});

        submit_task_recording(task_stack);

        return ECP_NONE;
    }

    uint64_t input_task(void* args, uint32_t thread_id)
    {
        // notify input loop
        {
            std::unique_lock<std::mutex> l(input_loop_sync.m);
            input_loop_sync.gather_input = true;
            input_loop_sync.input_gathered = false;
        }
        input_loop_sync.cv.notify_one();

        // wait for input
        {
            std::unique_lock<std::mutex> l(input_loop_sync.m);
            input_loop_sync.cv.wait(l, []{ return input_loop_sync.input_gathered; });
        }

        // add pressed keys as down
        uint32_t* down = &key_events[KEY_DOWN][0];
        for (uint32_t i = 0; i < num_events[KEY_PRESS]; ++i)
        {
            down[num_events[KEY_DOWN]] = key_events[KEY_PRESS][i];
            num_events[KEY_DOWN] = (num_events[KEY_DOWN] + 1) & (max_num_events[KEY_DOWN] - 1);
        }

        // remove released keys as down
        uint32_t* released = &key_events[KEY_RELEASE][0];
        for (uint32_t i = 0; i < num_events[KEY_RELEASE]; ++i)
        {
            for (uint32_t j = 0; j < num_events[KEY_DOWN]; ++j)
            {
                if (down[j] == released[i])
                {
                    down[j] = down[num_events[KEY_DOWN] - 1];
                    down[num_events[KEY_DOWN] - 1] = 0;
                    --num_events[KEY_DOWN];
                    break;
                }
            }
        }

        // pack to 16b events
        for (uint32_t i = 0; i < NUM_KEY_STATES; ++i)
        {
            __m128i e0 = _mm_load_si128((__m128i*) &key_events[i][0]);
            __m128i e1 = _mm_load_si128((__m128i*) &key_events[i][4]);
            __m128i lo = _mm_unpacklo_epi16(e0, e1);
            __m128i hi = _mm_unpackhi_epi16(e0, e1);
            hi = _mm_shuffle_epi32(hi, _MM_SHUFFLE(2, 1, 0, 3));
            __m128i packed = _mm_blend_epi16(hi, lo, 0b00110011);
            _mm_store_si128((__m128i*) &key_states[i][0], packed);
        }

        if (key_pressed(GLFW_KEY_ESCAPE))
            signal_shutdown();

        return ECP_INPUT1;
    }
}
