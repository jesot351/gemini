#include "gemini.h"
#include "systems/Input.h"
#include "managers/TaskScheduling.h"
#include "managers/Memory.h"

#include <iostream>
#include <mutex>
#include <condition_variable>

#include <GLFW/glfw3.h>

namespace SInput
{
    uint32_t system_id;
    MMemory::LinearAllocator32kb task_args_memory;
    input_loop_sync_t input_loop_sync;

    void init_input(uint32_t assigned_system_id)
    {
        system_id = assigned_system_id;
        task_args_memory.Init();
        submit_tasks(nullptr, 0);
    }

    void input_loop(GLFWwindow* window)
    {
        while (!MTaskScheduling::g_quit_request.load(std::memory_order_relaxed))
        {
            // wait for notification
            std::unique_lock<std::mutex> l(input_loop_sync.m);
            input_loop_sync.cv.wait(l, []{ return input_loop_sync.gather_input; });
            input_loop_sync.gather_input = false;

            // gather input
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

    uint64_t submit_tasks(void* args, uint32_t thread_id)
    {
        task_args_memory.Clear();

        MTaskScheduling::task_t task;

        task.execute = submit_tasks;
        task.args = nullptr;
        task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
        task.checkpoints_current_frame = MTaskScheduling::SCP_INPUT1;
        uint32_t stack_size = 1;
        MTaskScheduling::s_stacks[system_id][stack_size] = task;

        task.execute = input_task;
        task.args = nullptr;
        task.checkpoints_previous_frame = MTaskScheduling::SCP_RENDERING1;
        task.checkpoints_current_frame = MTaskScheduling::SCP_NONE;
        ++stack_size;
        MTaskScheduling::s_stacks[system_id][stack_size] = task;

        MTaskScheduling::s_stack_sizes[system_id].store((MTaskScheduling::s_iterations[system_id] << 7) | stack_size, std::memory_order_release);

        return MTaskScheduling::SCP_NONE;
    }

    uint64_t input_task(void* args, uint32_t thread_id)
    {
        // notify input loop
        {
            std::unique_lock<std::mutex> l(input_loop_sync.m); // necessary?
            input_loop_sync.gather_input = true;
            input_loop_sync.input_gathered = false;
        }
        input_loop_sync.cv.notify_one();

        // wait for input
        {
            std::unique_lock<std::mutex> l(input_loop_sync.m);
            input_loop_sync.cv.wait(l, []{ return input_loop_sync.input_gathered; });
        }

        // do stuff with input

        return MTaskScheduling::SCP_INPUT1;
    }
}
