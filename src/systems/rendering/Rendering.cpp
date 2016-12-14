#include "gemini.h"
#include "systems/rendering/Rendering.h"
#include "systems/rendering/Vulkan.h"
#include "managers/TaskScheduling.h"
#include "managers/Memory.h"
#include "data/Input.h"

#include <unistd.h> // usleep()
#include <iostream>
#include <chrono>

struct GLFWwindow;

namespace SRendering
{
    uint32_t system_id;
    MMemory::LinearAllocator32kb task_args_memory;

    void init_rendering(uint32_t assigned_system_id, GLFWwindow* window)
    {
        system_id = assigned_system_id;

        init_vulkan(window);

        task_args_memory.Init();
        submit_tasks(nullptr, 0);
    }

    void clear_rendering()
    {
        clear_vulkan();
    }

    std::atomic<uint32_t> num_executed_group1;
    std::atomic<uint32_t> num_executed_group2;
    std::atomic<uint32_t> num_executed_group3;

    std::atomic<double> perf_overlay_start_time;
    std::atomic<uint32_t> num_executed_perf_overlay;
    std::atomic<uint32_t> perf_overlay_write_offset;

    uint64_t submit_tasks(void* args, uint32_t thread_id)
    {
        task_args_memory.Clear();

        uint32_t stack_size = 1;
        MTaskScheduling::task_t task;

        // submit new tasks
        {
            task.execute = submit_tasks;
            task.args = nullptr;
            task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
            task.checkpoints_current_frame = MTaskScheduling::SCP_RENDERING_PRESENT;
            MTaskScheduling::s_stacks[system_id][stack_size] = task;
        }

        // present task
        {
            ++stack_size;
            task.execute = present_task;
            task.args = nullptr;
            task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
            task.checkpoints_current_frame = MTaskScheduling::SCP_RENDERING_WRITE_PERF_OVERLAY;
            MTaskScheduling::s_stacks[system_id][stack_size] = task;
        }

        // performance overlay task
        num_executed_perf_overlay.store(MTaskScheduling::NUM_WORKER_THREADS - 1, std::memory_order_relaxed);
        perf_overlay_start_time.store(0, std::memory_order_relaxed);
        perf_overlay_write_offset.store(0, std::memory_order_relaxed);
        for (uint32_t i = 0; i < MTaskScheduling::NUM_WORKER_THREADS; ++i)
        {
            ++stack_size;
            task.execute = write_perf_overlay_task;
            write_perf_overlay_task_args_t* args =  new(task_args_memory) write_perf_overlay_task_args_t;
            args->thread_log = i;
            args->iteration = MTaskScheduling::s_iterations[system_id];
            args->start_time = &perf_overlay_start_time;
            args->buffer_memory = get_mapped_overlay_vertex_buffer();
            args->write_offset = &perf_overlay_write_offset;
            args->counter = &num_executed_perf_overlay;
            task.args = args;
            task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
            task.checkpoints_current_frame = MTaskScheduling::SCP_RENDERING3;
            MTaskScheduling::s_stacks[system_id][stack_size] = task;
        }


        // 10 tasks in task group 3
        num_executed_group3.store(9, std::memory_order_relaxed);
        for (uint32_t i = 0; i < 10; ++i)
        {
            ++stack_size;
            MTaskScheduling::task_t task;
            task.execute = task_group3;
            task_group3_args_t* args = new(task_args_memory) task_group3_args_t;
            args->counter = &num_executed_group3;
            task.args = args;
            task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
            task.checkpoints_current_frame = MTaskScheduling::SCP_RENDERING2;
            MTaskScheduling::s_stacks[system_id][stack_size] = task;
        }

        /*
        // 4 independent tasks
        for (uint32_t i = 0; i < 4; ++i)
        {
            ++stack_size;
            MTaskScheduling::task_t task;
            task.execute = independent_task;
            independent_task_args_t* args = new(task_args_memory) independent_task_args_t;
            args->some_param = 42 - i;
            task.args = args;
            task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
            task.checkpoints_current_frame = MTaskScheduling::SCP_NONE;
            MTaskScheduling::s_stacks[system_id][stack_size] = task;
        }
        */

        // 10 tasks in task group 2
        num_executed_group2.store(9, std::memory_order_relaxed);
        for (uint32_t i = 0; i < 10; ++i)
        {
            ++stack_size;
            MTaskScheduling::task_t task;
            task.execute = task_group2;
            task_group2_args_t* args = new(task_args_memory) task_group2_args_t;
            args->counter = &num_executed_group2;
            task.args = args;
            task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
            task.checkpoints_current_frame = MTaskScheduling::SCP_PHYSICS4 | MTaskScheduling::SCP_RENDERING1;
            MTaskScheduling::s_stacks[system_id][stack_size] = task;
        }

        /*
        // 4 independent tasks
        for (uint32_t i = 0; i < 4; ++i)
        {
            ++stack_size;
            MTaskScheduling::task_t task;
            task.execute = independent_task;
            independent_task_args_t* args = new(task_args_memory) independent_task_args_t;
            args->some_param = 42 + i;
            task.args = args;
            task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
            task.checkpoints_current_frame = MTaskScheduling::SCP_NONE;
            MTaskScheduling::s_stacks[system_id][stack_size] = task;
        }
        */

        // 10 tasks in task group 1
        num_executed_group1.store(9, std::memory_order_relaxed);
        for (uint32_t i = 0; i < 10; ++i)
        {
            ++stack_size;
            MTaskScheduling::task_t task;
            task.execute = task_group1;
            task_group1_args_t* args = new(task_args_memory) task_group1_args_t;
            args->counter = &num_executed_group1;
            task.args = args;
            task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
            task.checkpoints_current_frame = MTaskScheduling::SCP_INPUT1;
            MTaskScheduling::s_stacks[system_id][stack_size] = task;
        }

        /*
        // 4 independent tasks
        for (uint32_t i = 0; i < 4; ++i)
        {
            ++stack_size;
            MTaskScheduling::task_t task;
            task.execute = independent_task;
            independent_task_args_t* args = new(task_args_memory) independent_task_args_t;
            args->some_param = 42 + i;
            task.args = args;
            task.checkpoints_previous_frame = MTaskScheduling::SCP_NONE;
            task.checkpoints_current_frame = MTaskScheduling::SCP_NONE;
            MTaskScheduling::s_stacks[system_id][stack_size] = task;
        }
        */

        MTaskScheduling::s_stack_sizes[system_id].store((MTaskScheduling::s_iterations[system_id] << 7) | stack_size, std::memory_order_release);

        return MTaskScheduling::SCP_NONE;
    }

    uint64_t independent_task(void* args, uint32_t thread_id)
    {
        usleep(100);

        return MTaskScheduling::SCP_NONE;
    }

    uint64_t task_group1(void* args, uint32_t thread_id)
    {
        task_group1_args_t* pargs = (task_group1_args_t*) args;

        usleep(100);

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = MTaskScheduling::SCP_NONE;
        if (count == 0)
            reached_checkpoints = MTaskScheduling::SCP_RENDERING1;

        return reached_checkpoints;
    }

    uint64_t task_group2(void* args, uint32_t thread_id)
    {
        task_group2_args_t* pargs = (task_group2_args_t*) args;

        usleep(100);

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = MTaskScheduling::SCP_NONE;
        if (count == 0)
            reached_checkpoints = MTaskScheduling::SCP_RENDERING2;

        return reached_checkpoints;
    }

    uint64_t task_group3(void* args, uint32_t thread_id)
    {
        task_group3_args_t* pargs = (task_group3_args_t*) args;

        usleep(100);

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = MTaskScheduling::SCP_NONE;
        if (count == 0)
            reached_checkpoints = MTaskScheduling::SCP_RENDERING3;

        return reached_checkpoints;
    }

    uint64_t write_perf_overlay_task(void* args, uint32_t thread_id)
    {
        write_perf_overlay_task_args_t* pargs = (write_perf_overlay_task_args_t*) args;

        uint32_t thread_log = pargs->thread_log;
        uint32_t it = (pargs->iteration - 2) & 0x03; // all task from two iterations ago are finished
        uint32_t num_logged_items = MTaskScheduling::profiling_i[it][thread_log];
        MTaskScheduling::profiling_i[it][thread_log] = 0; // reset log

        if (key_pressed(GLFW_KEY_P))
        {
            MTaskScheduling::profiling_item_t* log = &MTaskScheduling::profiling_log[it][thread_log][0];

            double start_time = 0;
            if (pargs->start_time->compare_exchange_weak(start_time, log[0].sched_start, std::memory_order_acq_rel))
            {
                start_time = log[0].sched_start;
            }

            uint32_t num_vertices = 4 * num_logged_items * 2; // * 2 for scheduling blocks
            uint32_t write_offset = pargs->write_offset->fetch_add(num_vertices, std::memory_order_relaxed);

            vertex_t* mapped_vertex_buffer = reinterpret_cast<vertex_t*>(pargs->buffer_memory);
            vertex_t* overlay_vertices = &mapped_vertex_buffer[write_offset];

            // doubles for increased accuracy during time calculations ?
            double lane_margin = 0.03d;
            double range = 8000.0d;//16666.666666667d; // us, 1/60 s in view
            double time_scale = (2.0d - 2.0d * lane_margin) / range;
            double start_offset_x = -1.0d + lane_margin;
            double start_offset_y = -1.0d + lane_margin;

            float lane_height = 0.05d;
            float t = start_offset_y + thread_log * (lane_margin + lane_height);
            float b = t + lane_height;

            glm::vec3 stack_colors[] = {
                {0.0f, 0.8f, 0.0f},
                {0.0f, 0.8f, 1.0f},
                {1.0f, 1.0f, 0.0f},
                {1.0f, 0.8f, 0.0f},
                {1.0f, 0.2f, 0.0f},
            };

            double total_sched_time = 0;
            double total_exec_time = 0;
            uint64_t total_sched_clock_cycles = 0;
            for (uint32_t i = 0; i < num_logged_items; ++i)
            {
                // task execution block
                float l = (float) ( start_offset_x + (log[i].sched_end - start_time) * time_scale );
                float r = (float) ( start_offset_x + (log[i].exec_end - start_time) * time_scale );
                glm::vec3 color = stack_colors[log[i].stack];

                overlay_vertices[i * 4 + 0] = {{l, t}, color};
                overlay_vertices[i * 4 + 1] = {{l, b}, color};
                overlay_vertices[i * 4 + 2] = {{r, b}, color};
                overlay_vertices[i * 4 + 3] = {{r, t}, color};

                // scheduling block (put last to write on top of task blocks)
                l = (float) ( start_offset_x + (log[i].sched_start - start_time) * time_scale );
                double sched_time = std::max(log[i].sched_end - log[i].sched_start,
                                             2.0d / (double) WIDTH / time_scale); // at least one pixel
                r = (float) ( start_offset_x + (log[i].sched_start + sched_time - start_time) * time_scale );
                color = {1.0f, 1.0f, 1.0f};

                overlay_vertices[(num_logged_items + i) * 4 + 0] = {{l, t}, color};
                overlay_vertices[(num_logged_items + i) * 4 + 1] = {{l, b}, color};
                overlay_vertices[(num_logged_items + i) * 4 + 2] = {{r, b}, color};
                overlay_vertices[(num_logged_items + i) * 4 + 3] = {{r, t}, color};

                // performance characteristics
                total_sched_time += log[i].sched_end - log[i].sched_start;
                total_exec_time += log[i].exec_end - log[i].sched_end;
                total_sched_clock_cycles += log[i].rdtscp_sched;
            }

            if (thread_log == 0)
            {
                std::cout << "Scheduling overhead: " << total_sched_time / total_exec_time << "\n";
                std::cout << "Scheduling clock cycles: " << total_sched_clock_cycles / num_logged_items << "\n";
            }

            /*
            float offset = thread_log * 0.15f;
            overlay_vertices[0] = {{offset + -0.9f, -0.9f}, {1.0f, 0.0f, 0.0f}};
            overlay_vertices[1] = {{offset + -0.9f, -0.8f}, {0.0f, 0.0f, 1.0f}};
            overlay_vertices[2] = {{offset + -0.8f, -0.8f}, {1.0f, 1.0f, 0.0f}};
            overlay_vertices[3] = {{offset + -0.8f, -0.9f}, {0.0f, 1.0f, 0.0f}};
            */
        }

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = MTaskScheduling::SCP_NONE;
        if (count == 0)
            reached_checkpoints = MTaskScheduling::SCP_RENDERING_WRITE_PERF_OVERLAY;

        return reached_checkpoints;
    }

    uint64_t present_task(void* args, uint32_t thread_id)
    {
        static std::chrono::time_point<std::chrono::high_resolution_clock> prev_t;
        auto t = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::micro> frame_delta = t - prev_t;

        if (key_pressed(GLFW_KEY_P))
        {
            double ms = frame_delta.count() / 1000.0d;
            std::cout << "frame: " << ms << " ms, " << 1000 / ms << " fps" << std::endl;
        }

        prev_t = t;

        update_transforms((float) frame_delta.count());
        draw_frame();

        return MTaskScheduling::SCP_RENDERING_PRESENT;
    }
}
