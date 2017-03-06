#include "gemini.h"
#include "systems/rendering/Rendering.h"
#include "systems/rendering/Vulkan.h"
#include "managers/TaskScheduling.h"
#include "managers/Memory.h"
#include "data/Input.h"

#include <unistd.h> // usleep()
#include <iostream>
#include <chrono>

using namespace MTaskScheduling;

struct GLFWwindow;

namespace SRendering
{
    task_stack_t* task_stack;
    MMemory::LinearAllocator32kb task_args_memory;

    void init_rendering(task_stack_t* assigned_task_stack, GLFWwindow* window)
    {
        task_stack = assigned_task_stack;

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

        begin_task_recording(task_stack);

        record_task(task_stack, {submit_tasks, nullptr, ECP_NONE, ECP_RENDERING_PRESENT});
        record_task(task_stack, {present_task, nullptr, ECP_NONE, ECP_RENDERING_WRITE_PERF_OVERLAY});

        // performance overlay task
        num_executed_perf_overlay.store(NUM_WORKER_THREADS - 1, std::memory_order_relaxed);
        perf_overlay_start_time.store(0, std::memory_order_relaxed);
        perf_overlay_write_offset.store(0, std::memory_order_relaxed);
        for (uint32_t i = 0; i < NUM_WORKER_THREADS; ++i)
        {
            write_perf_overlay_task_args_t* args =  new(task_args_memory) write_perf_overlay_task_args_t;
            args->thread_log = i;
            args->iteration = s_iterations[task_stack->index];
            args->start_time = &perf_overlay_start_time;
            args->buffer_memory = get_mapped_overlay_vertex_buffer();
            args->write_offset = &perf_overlay_write_offset;
            args->counter = &num_executed_perf_overlay;

            record_task(task_stack, {write_perf_overlay_task, args, ECP_NONE, ECP_RENDERING3});
        }


        // 10 tasks in task group 3
        num_executed_group3.store(9, std::memory_order_relaxed);
        for (uint32_t i = 0; i < 10; ++i)
        {
            task_group3_args_t* args = new(task_args_memory) task_group3_args_t;
            args->counter = &num_executed_group3;

            record_task(task_stack, {task_group3, args, ECP_NONE, ECP_RENDERING2});
        }

        // 4 independent tasks
        for (uint32_t i = 0; i < 4; ++i)
        {
            independent_task_args_t* args = new(task_args_memory) independent_task_args_t;
            args->some_param = 42 - i;

            record_task(task_stack, {independent_task, args, ECP_NONE, ECP_NONE});
        }

        // 10 tasks in task group 2
        num_executed_group2.store(9, std::memory_order_relaxed);
        for (uint32_t i = 0; i < 10; ++i)
        {
            task_group2_args_t* args = new(task_args_memory) task_group2_args_t;
            args->counter = &num_executed_group2;

            record_task(task_stack, {task_group2, args, ECP_NONE, ECP_PHYSICS4 | ECP_RENDERING1});
        }

        // 4 independent tasks
        for (uint32_t i = 0; i < 4; ++i)
        {
            independent_task_args_t* args = new(task_args_memory) independent_task_args_t;
            args->some_param = 42 + i;

            record_task(task_stack, {independent_task, args, ECP_NONE, ECP_NONE});
        }

        // 10 tasks in task group 1
        num_executed_group1.store(9, std::memory_order_relaxed);
        for (uint32_t i = 0; i < 10; ++i)
        {
            task_group1_args_t* args = new(task_args_memory) task_group1_args_t;
            args->counter = &num_executed_group1;

            record_task(task_stack, {task_group1, args, ECP_NONE, ECP_INPUT1});
        }

        // 4 independent tasks
        for (uint32_t i = 0; i < 4; ++i)
        {
            independent_task_args_t* args = new(task_args_memory) independent_task_args_t;
            args->some_param = 42 + i;

            record_task(task_stack, {independent_task, args, ECP_NONE, ECP_NONE});
        }

        submit_task_recording(task_stack);

        return ECP_NONE;
    }

    uint64_t independent_task(void* args, uint32_t thread_id)
    {
        simulate_work();

        return ECP_NONE;
    }

    uint64_t task_group1(void* args, uint32_t thread_id)
    {
        task_group1_args_t* pargs = (task_group1_args_t*) args;

        simulate_work();

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = ECP_NONE;
        if (count == 0)
            reached_checkpoints = ECP_RENDERING1;

        return reached_checkpoints;
    }

    uint64_t task_group2(void* args, uint32_t thread_id)
    {
        task_group2_args_t* pargs = (task_group2_args_t*) args;

        simulate_work();

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = ECP_NONE;
        if (count == 0)
            reached_checkpoints = ECP_RENDERING2;

        return reached_checkpoints;
    }

    uint64_t task_group3(void* args, uint32_t thread_id)
    {
        task_group3_args_t* pargs = (task_group3_args_t*) args;

        simulate_work();

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = ECP_NONE;
        if (count == 0)
            reached_checkpoints = ECP_RENDERING3;

        return reached_checkpoints;
    }

    uint64_t write_perf_overlay_task(void* args, uint32_t thread_id)
    {
        write_perf_overlay_task_args_t* pargs = (write_perf_overlay_task_args_t*) args;

        uint32_t thread_log = pargs->thread_log;
        uint32_t it = (pargs->iteration - 2) & 0x03; // all task from two iterations ago are finished
        uint32_t num_logged_items = profiling_i[it][thread_log];
        profiling_i[it][thread_log] = 0; // reset log

        if (key_pressed(GLFW_KEY_P))
        {
            profiling_item_t* log = &profiling_log[it][thread_log][0];

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

            const static glm::vec3 stack_colors[] = {
                {0.9176f       , 0.6000f / 2.0f, 0.6000f / 2.0f},
                {0.6235f / 4.0f, 0.7725f / 2.0f, 0.9098f       },
                {0.7137f / 2.0f, 0.8431f       , 0.6588f / 2.0f},
                {1.0000f       , 0.8980f       , 0.6000f / 2.0f},
                {0.7059f       , 0.6549f / 2.0f, 0.8392f       },
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
        }

        uint32_t count = pargs->counter->fetch_sub(1, std::memory_order_release);
        uint64_t reached_checkpoints = ECP_NONE;
        if (count == 0)
            reached_checkpoints = ECP_RENDERING_WRITE_PERF_OVERLAY;

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

        return ECP_RENDERING_PRESENT;
    }
}
